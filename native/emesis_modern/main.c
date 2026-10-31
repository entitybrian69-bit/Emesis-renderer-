/**
 * Created by: artDev, DuyKhanhTran
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "GL/gl.h"
#include "GL/glext.h"
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include "unordered_map/int_hash.h"
#include "string_utils.h"
#include "proc.h"
#include "egl.h"
#include "glformats.h"
#include "main.h"
#include "swizzle.h"
#include "libraryinternal.h"
#include "env.h"

#ifndef GL_QUERY_BUFFER
#define GL_QUERY_BUFFER 0x9192
#endif
#ifndef GL_BGR_INTEGER
#define GL_BGR_INTEGER 0x8D9A
#endif
#ifndef GL_BGRA_INTEGER
#define GL_BGRA_INTEGER 0x8D9B
#endif
#ifndef GL_STENCIL_INDEX
#define GL_STENCIL_INDEX 0x1901
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_MAP_PERSISTENT_BIT_EXT
#define GL_MAP_PERSISTENT_BIT_EXT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT_EXT
#define GL_MAP_COHERENT_BIT_EXT 0x0080
#endif

#define EMESIS_UPLOAD_RING_SIZE_32      (8u * 1024u * 1024u)
#define EMESIS_UPLOAD_RING_SIZE_64      (32u * 1024u * 1024u)
#define EMESIS_UPLOAD_RING_MIN_DST_SIZE (64u * 1024u)

static unsigned int gl_major = 3;
static unsigned int gl_minor = 3;
static bool noerror = true;
static bool debug = false;
static bool never_flush_buffers = true;
static bool coherent_dynamic_storage = false;
static bool use_upload_ring = true;

extern void emesis_fbo_on_delete_textures(context_t* ctx, GLsizei n, const GLuint* textures);

static bool extract_gl_version(const char* version) {
    if (!version) return false;
    unsigned int major = 0, minor = 0;
    if (sscanf(version, "%u.%u", &major, &minor) != 2) return false;
    if ((major < 2 || major > 4) || minor > 9) {
        return false;
    }
    gl_major = major;
    gl_minor = minor;
    return true;
}

static bool is_depth_fmt_fast(GLenum fmt) {
    switch (fmt) {
        case GL_DEPTH_COMPONENT:
        case GL_DEPTH_STENCIL:
        case GL_DEPTH_COMPONENT16:
        case GL_DEPTH_COMPONENT24:
        case GL_DEPTH_COMPONENT32:
        case GL_DEPTH_COMPONENT32F:
        case GL_DEPTH24_STENCIL8:
        case GL_DEPTH32F_STENCIL8:
            return true;
        default:
            return false;
    }
}

static void record_tex_format_fast(context_t* ctx, GLenum target, GLenum internalformat, GLenum format) {
    if (!ctx) return;
    GLuint tex = emesis_get_bound_texture(target);
    if (tex == 0 || tex >= EMESIS_FAST_TEX_MAX) return;
    ctx->tex_intfmt_fast[tex] = internalformat ? internalformat : GL_RGBA8;
    if (is_depth_fmt_fast(internalformat) || is_depth_fmt_fast(format)) {
        ctx->tex_flags_fast[tex] |= EMESIS_TEX_FLAG_DEPTH;
    } else {
        ctx->tex_flags_fast[tex] &= (uint8_t)~EMESIS_TEX_FLAG_DEPTH;
    }
}

static void buf_set_coherent(context_t* ctx, GLuint buf_id, bool is_coherent) {
    if (!ctx || buf_id == 0) return;
    if (buf_id < EMESIS_FAST_BUF_MAX) {
        if (is_coherent) ctx->buf_flags_fast[buf_id] |= EMESIS_BUF_FLAG_COHERENT_MAPPED;
        else ctx->buf_flags_fast[buf_id] &= (uint8_t)~EMESIS_BUF_FLAG_COHERENT_MAPPED;
        return;
    }
    if (!ctx->buf_coherent_map) return;
    if (is_coherent) unordered_map_put(ctx->buf_coherent_map, (void*)(uintptr_t)buf_id, (void*)1);
    else unordered_map_remove(ctx->buf_coherent_map, (void*)(uintptr_t)buf_id);
}

static bool buf_is_coherent(context_t* ctx, GLuint buf_id) {
    if (!ctx || buf_id == 0) return false;
    if (buf_id < EMESIS_FAST_BUF_MAX) {
        return (ctx->buf_flags_fast[buf_id] & EMESIS_BUF_FLAG_COHERENT_MAPPED) != 0;
    }
    return ctx->buf_coherent_map && unordered_map_get(ctx->buf_coherent_map, (void*)(uintptr_t)buf_id) != NULL;
}

static void buf_set_size(context_t* ctx, GLuint buf_id, size_t sz) {
    if (!ctx || buf_id == 0) return;
    if (buf_id < EMESIS_FAST_BUF_MAX) {
        ctx->buf_size_fast[buf_id] = sz;
        return;
    }
    if (!ctx->buf_size_map) return;
    if (sz != 0) unordered_map_put(ctx->buf_size_map, (void*)(uintptr_t)buf_id, (void*)(uintptr_t)sz);
    else unordered_map_remove(ctx->buf_size_map, (void*)(uintptr_t)buf_id);
}

static size_t buf_get_size(context_t* ctx, GLuint buf_id) {
    if (!ctx || buf_id == 0) return 0;
    if (buf_id < EMESIS_FAST_BUF_MAX) {
        return ctx->buf_size_fast[buf_id];
    }
    return ctx->buf_size_map ? (size_t)(uintptr_t)unordered_map_get(ctx->buf_size_map, (void*)(uintptr_t)buf_id) : 0;
}

static void buf_invalidate_tbo_bindings(context_t* ctx, GLuint buf_id) {
    if (!ctx || buf_id == 0) return;
    if (buf_id < EMESIS_FAST_BUF_MAX) {
        if ((ctx->buf_flags_fast[buf_id] & EMESIS_BUF_FLAG_TBO_ATTACHED) == 0) return;
        ctx->buf_flags_fast[buf_id] &= (uint8_t)~EMESIS_BUF_FLAG_TBO_ATTACHED;
    }
    for (size_t t = 1; t < EMESIS_FAST_TEX_MAX; t++) {
        if (ctx->tex_tbo_buf_fast[t] == buf_id) {
            ctx->tex_tbo_buf_fast[t] = 0;
            ctx->tex_tbo_fmt_fast[t] = 0;
        }
    }
}

static void vao_set_ebo(context_t* ctx, GLuint vao, GLuint ebo) {
    if (vao == 0) {
        ctx->default_vao_ebo = ebo;
        ctx->default_vao_ebo_valid = true;
    } else if (vao < EMESIS_FAST_VAO_MAX) {
        ctx->vao_ebo_fast[vao] = ebo;
        ctx->vao_ebo_valid_fast[vao] = 1;
    } else if (ctx->vao_ebo_map) {
        if (ebo != 0) unordered_map_put(ctx->vao_ebo_map, (void*)(uintptr_t)vao, (void*)(uintptr_t)ebo);
        else unordered_map_remove(ctx->vao_ebo_map, (void*)(uintptr_t)vao);
    }
}

static bool vao_is_ebo_cached(context_t* ctx, GLuint vao, GLuint ebo) {
    if (vao == 0) {
        return ctx->default_vao_ebo_valid && ctx->default_vao_ebo == ebo;
    }
    if (vao < EMESIS_FAST_VAO_MAX) {
        return ctx->vao_ebo_valid_fast[vao] != 0 && ctx->vao_ebo_fast[vao] == ebo;
    }
    return false;
}

INTERNAL void emesis_invalidate_vao_ebo_cache(context_t* ctx) {
    if (!ctx) return;
    GLuint vao = ctx->current_vao;
    if (vao == 0) ctx->default_vao_ebo_valid = false;
    else if (vao < EMESIS_FAST_VAO_MAX) ctx->vao_ebo_valid_fast[vao] = 0;
}

static GLuint vao_get_ebo(context_t* ctx, GLuint vao) {
    if (vao == 0) return ctx->default_vao_ebo;
    if (vao < EMESIS_FAST_VAO_MAX) return ctx->vao_ebo_fast[vao];
    if (ctx->vao_ebo_map) return (GLuint)(uintptr_t)unordered_map_get(ctx->vao_ebo_map, (void*)(uintptr_t)vao);
    return 0;
}

void glClearDepth(GLdouble depth) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    es3_functions.glClearDepthf((GLfloat)depth);
}

INTERNAL int get_buffer_index(GLenum buffer) {
    switch (buffer) {
        case GL_ARRAY_BUFFER: return 0;
        case GL_COPY_READ_BUFFER: return 1;
        case GL_COPY_WRITE_BUFFER: return 2;
        case GL_PIXEL_PACK_BUFFER: return 3;
        case GL_PIXEL_UNPACK_BUFFER: return 4;
        case GL_TRANSFORM_FEEDBACK_BUFFER: return 5;
        case GL_UNIFORM_BUFFER: return 6;
        case GL_SHADER_STORAGE_BUFFER: return 7;
        case GL_DRAW_INDIRECT_BUFFER: return 8;
        case GL_DISPATCH_INDIRECT_BUFFER: return 9;
        case GL_ATOMIC_COUNTER_BUFFER: return 10;
        case GL_TEXTURE_BUFFER: return 11;
        case GL_ELEMENT_ARRAY_BUFFER: return -2; /* Tracked per-VAO */
        default: return -1;
    }
}

INTERNAL int get_base_buffer_index(GLenum buffer) {
    switch (buffer) {
        case GL_ATOMIC_COUNTER_BUFFER: return 0;
        case GL_SHADER_STORAGE_BUFFER: return 1;
        case GL_TRANSFORM_FEEDBACK_BUFFER: return 2;
        case GL_UNIFORM_BUFFER: return 3;
        default: return -1;
    }
}

INTERNAL GLenum get_base_buffer_enum(int buffer_index) {
    switch (buffer_index) {
        case 0: return GL_ATOMIC_COUNTER_BUFFER;
        case 1: return GL_SHADER_STORAGE_BUFFER;
        case 2: return GL_TRANSFORM_FEEDBACK_BUFFER;
        case 3: return GL_UNIFORM_BUFFER;
        default: return (GLenum)-1;
    }
}

static GLuint get_bound_buffer_for_target(context_t* ctx, GLenum target) {
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        return vao_get_ebo(ctx, ctx->current_vao);
    }
    int idx = get_buffer_index(target);
    if (idx >= 0) return ctx->bound_buffers[idx];
    return 0;
}

INTERNAL int isProxyTexture(GLenum target) {
    switch (target) {
        case GL_PROXY_TEXTURE_1D:
        case GL_PROXY_TEXTURE_2D:
        case GL_PROXY_TEXTURE_3D:
        case GL_PROXY_TEXTURE_RECTANGLE_ARB:
            return 1;
    }
    return 0;
}

INTERNAL int emesis_tex_target_index(GLenum target) {
    switch (target) {
        case GL_TEXTURE_1D:
        case GL_TEXTURE_2D: return 0;
        case GL_TEXTURE_3D: return 1;
        case GL_TEXTURE_2D_ARRAY: return 2;
        case GL_TEXTURE_CUBE_MAP:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z: return 3;
        case GL_TEXTURE_CUBE_MAP_ARRAY: return 4;
        case GL_TEXTURE_BUFFER: return 5;
        case GL_TEXTURE_2D_MULTISAMPLE: return 6;
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY: return 7;
        case GL_TEXTURE_EXTERNAL_OES: return 8;
        default: return -1;
    }
}

INTERNAL GLenum emesis_tex_index_to_target(int idx) {
    switch (idx) {
        case 0: return GL_TEXTURE_2D;
        case 1: return GL_TEXTURE_3D;
        case 2: return GL_TEXTURE_2D_ARRAY;
        case 3: return GL_TEXTURE_CUBE_MAP;
        case 4: return GL_TEXTURE_CUBE_MAP_ARRAY;
        case 5: return GL_TEXTURE_BUFFER;
        case 6: return GL_TEXTURE_2D_MULTISAMPLE;
        case 7: return GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
        case 8: return GL_TEXTURE_EXTERNAL_OES;
        default: return GL_TEXTURE_2D;
    }
}

INTERNAL GLenum emesis_resolve_tex_target(context_t* ctx, GLuint texture) {
    if (ctx && texture > 0 && texture < EMESIS_FAST_TEX_MAX && ctx->tex_target_fast[texture] != 0) {
        return emesis_tex_index_to_target((int)ctx->tex_target_fast[texture] - 1);
    }
    return GL_TEXTURE_2D;
}

INTERNAL GLenum get_textarget_query_param(GLenum target) {
    switch (target) {
        case GL_TEXTURE_1D:
        case GL_TEXTURE_2D:
            return GL_TEXTURE_BINDING_2D;
        case GL_TEXTURE_2D_MULTISAMPLE:
            return GL_TEXTURE_BINDING_2D_MULTISAMPLE;
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
            return GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY;
        case GL_TEXTURE_3D:
            return GL_TEXTURE_BINDING_3D;
        case GL_TEXTURE_2D_ARRAY:
            return GL_TEXTURE_BINDING_2D_ARRAY;
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        case GL_TEXTURE_CUBE_MAP:
            return GL_TEXTURE_BINDING_CUBE_MAP;
        case GL_TEXTURE_CUBE_MAP_ARRAY:
            return GL_TEXTURE_BINDING_CUBE_MAP_ARRAY;
        case GL_TEXTURE_BUFFER:
            return GL_TEXTURE_BUFFER_BINDING;
        default:
            return 0;
    }
}

INTERNAL GLuint emesis_get_bound_texture(GLenum target) {
    context_t* ctx = current_context;
    if (!ctx) return 0;
    int t_idx = emesis_tex_target_index(target);
    GLuint unit = ctx->active_tex_unit;
    if (t_idx >= 0 && unit < MAX_TMUS) {
        uint16_t bit = (uint16_t)(1u << t_idx);
        if ((ctx->bound_textures_valid[unit] & bit) != 0) {
            return ctx->bound_textures[unit][t_idx];
        }
        GLuint cached = ctx->bound_textures[unit][t_idx];
        if (cached != 0) return cached;
    }
    GLenum getter = get_textarget_query_param(target);
    if (getter == 0) return 0;
    GLint queried = 0;
    es3_functions.glGetIntegerv(getter, &queried);
    if (t_idx >= 0 && unit < MAX_TMUS) {
        ctx->bound_textures[unit][t_idx] = (GLuint)(queried > 0 ? queried : 0);
        ctx->bound_textures_valid[unit] |= (uint16_t)(1u << t_idx);
    }
    return (GLuint)(queried > 0 ? queried : 0);
}

INTERNAL GLuint emesis_get_current_ebo(void) {
    context_t* ctx = current_context;
    if (!ctx) return 0;
    return vao_get_ebo(ctx, ctx->current_vao);
}

/* --- Pure ES 3.0 zero-readback EBO mirror maintenance (cold/no-op on ES 3.1+ and OES_base_vertex) --- */
static es30_ebo_mirror_t* get_or_create_es30_mirror(context_t* ctx, GLuint buf) {
    if (!ctx || !ctx->es30_ebo_mirror_map || buf == 0) return NULL;
    es30_ebo_mirror_t* m = unordered_map_get(ctx->es30_ebo_mirror_map, (void*)(uintptr_t)buf);
    if (!m) {
        m = calloc(1, sizeof(es30_ebo_mirror_t));
        if (m) unordered_map_put(ctx->es30_ebo_mirror_map, (void*)(uintptr_t)buf, m);
    }
    return m;
}

void emesis_es30_on_buffer_data(context_t* ctx, GLenum target, GLsizeiptr size, const void* data) {
    if (!ctx || !ctx->es30_needs_ebo_mirror || size < 0) return;
    GLuint buf = get_bound_buffer_for_target(ctx, target);
    if (buf == 0) return;
    if (target != GL_ELEMENT_ARRAY_BUFFER &&
        (!ctx->es30_ebo_mirror_map || !unordered_map_get(ctx->es30_ebo_mirror_map, (void*)(uintptr_t)buf))) {
        return;
    }
    es30_ebo_mirror_t* m = get_or_create_es30_mirror(ctx, buf);
    if (!m) return;
    size_t sz = (size_t)size;
    if (sz > m->cap) {
        uint8_t* p = realloc(m->data, sz);
        if (!p) {
            m->valid = false;
            return;
        }
        m->data = p;
        m->cap = sz;
    }
    m->size = sz;
    if (data != NULL && sz > 0) {
        memcpy(m->data, data, sz);
        m->valid = true;
    } else {
        m->valid = false;
    }
}

void emesis_es30_on_buffer_sub_data(context_t* ctx, GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    if (!ctx || !ctx->es30_needs_ebo_mirror || !ctx->es30_ebo_mirror_map || !data || offset < 0 || size <= 0) return;
    GLuint buf = get_bound_buffer_for_target(ctx, target);
    if (buf == 0) return;
    es30_ebo_mirror_t* m = unordered_map_get(ctx->es30_ebo_mirror_map, (void*)(uintptr_t)buf);
    if (!m || !m->valid) return;
    size_t off = (size_t)offset;
    size_t sz = (size_t)size;
    if (off + sz <= m->size) {
        memcpy(m->data + off, data, sz);
    } else {
        m->valid = false;
    }
}

void emesis_es30_on_copy_buffer_sub_data(context_t* ctx, GLenum readTarget, GLenum writeTarget,
                                         GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
    if (!ctx || !ctx->es30_needs_ebo_mirror || !ctx->es30_ebo_mirror_map || size <= 0) return;
    GLuint w_buf = get_bound_buffer_for_target(ctx, writeTarget);
    if (w_buf == 0) return;
    es30_ebo_mirror_t* wm = unordered_map_get(ctx->es30_ebo_mirror_map, (void*)(uintptr_t)w_buf);
    if (!wm) return;
    GLuint r_buf = get_bound_buffer_for_target(ctx, readTarget);
    es30_ebo_mirror_t* rm = (r_buf != 0)
        ? unordered_map_get(ctx->es30_ebo_mirror_map, (void*)(uintptr_t)r_buf)
        : NULL;
    if (wm->valid && rm && rm->valid && readOffset >= 0 && writeOffset >= 0 &&
        (size_t)readOffset + (size_t)size <= rm->size &&
        (size_t)writeOffset + (size_t)size <= wm->size) {
        memmove(wm->data + (size_t)writeOffset, rm->data + (size_t)readOffset, (size_t)size);
    } else {
        wm->valid = false;
    }
}

void emesis_es30_invalidate_buffer(context_t* ctx, GLuint buf) {
    if (!ctx || !ctx->es30_ebo_mirror_map || buf == 0) return;
    es30_ebo_mirror_t* m = unordered_map_get(ctx->es30_ebo_mirror_map, (void*)(uintptr_t)buf);
    if (m) m->valid = false;
}

void emesis_es30_delete_buffer(context_t* ctx, GLuint buf) {
    if (!ctx || !ctx->es30_ebo_mirror_map || buf == 0) return;
    es30_ebo_mirror_t* m = unordered_map_remove(ctx->es30_ebo_mirror_map, (void*)(uintptr_t)buf);
    if (m) {
        free(m->data);
        free(m);
    }
}

const uint8_t* emesis_es30_get_ebo_cpu_ptr(context_t* ctx, GLuint ebo, size_t min_needed_end) {
    if (!ctx || ebo == 0) return NULL;
    if (!ctx->es30_ebo_mirror_map) {
        ctx->es30_ebo_mirror_map = alloc_intmap_safe();
        if (!ctx->es30_ebo_mirror_map) return NULL;
    }
    es30_ebo_mirror_t* m = get_or_create_es30_mirror(ctx, ebo);
    if (!m) return NULL;
    if (m->valid && min_needed_end <= m->size) {
        return m->data;
    }
    GLint64 buf_size = 0;
    if (es3_functions.glGetBufferParameteri64v) {
        es3_functions.glGetBufferParameteri64v(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &buf_size);
    } else if (es3_functions.glGetBufferParameteriv) {
        GLint sz32 = 0;
        es3_functions.glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &sz32);
        buf_size = sz32;
    }
    size_t bsz = (buf_size > 0) ? (size_t)buf_size : min_needed_end;
    if (bsz < min_needed_end) return NULL;
    if (bsz > m->cap) {
        uint8_t* p = realloc(m->data, bsz);
        if (!p) return NULL;
        m->data = p;
        m->cap = bsz;
    }
    const void* gpu_ptr = es3_functions.glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)bsz, GL_MAP_READ_BIT);
    if (!gpu_ptr) return NULL;
    memcpy(m->data, gpu_ptr, bsz);
    es3_functions.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    m->size = bsz;
    m->valid = true;
    return m->data;
}

void glActiveTexture(GLenum texture) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (texture >= GL_TEXTURE0 && (texture - GL_TEXTURE0) < MAX_TMUS) {
        GLuint unit = (GLuint)(texture - GL_TEXTURE0);
        if (ctx->active_tex_unit_valid && ctx->active_tex_unit == unit) {
            return;
        }
        es3_functions.glActiveTexture(texture);
        ctx->active_tex_unit = unit;
        ctx->active_tex_unit_valid = true;
        return;
    }
    es3_functions.glActiveTexture(texture);
}

void glBindTexture(GLenum target, GLuint texture) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    int t_idx = emesis_tex_target_index(target);
    if (texture > 0 && texture < EMESIS_FAST_TEX_MAX && t_idx >= 0) {
        ctx->tex_target_fast[texture] = (uint8_t)(t_idx + 1);
    }
    GLuint u = ctx->active_tex_unit;
    if (t_idx >= 0 && u < MAX_TMUS) {
        uint16_t bit = (uint16_t)(1u << t_idx);
        if ((ctx->bound_textures_valid[u] & bit) != 0 && ctx->bound_textures[u][t_idx] == texture) {
            return;
        }
        es3_functions.glBindTexture(target, texture);
        ctx->bound_textures[u][t_idx] = texture;
        ctx->bound_textures_valid[u] |= bit;
        return;
    }
    es3_functions.glBindTexture(target, texture);
}

void glBindSampler(GLuint unit, GLuint sampler) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (unit < MAX_TMUS) {
        uint32_t bit = 1u << unit;
        if ((ctx->bound_samplers_valid_mask & bit) != 0 && ctx->bound_samplers[unit] == sampler) {
            return;
        }
        es3_functions.glBindSampler(unit, sampler);
        ctx->bound_samplers[unit] = sampler;
        ctx->bound_samplers_valid_mask |= bit;
        return;
    }
    es3_functions.glBindSampler(unit, sampler);
}

void glDeleteSamplers(GLsizei n, const GLuint* samplers) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !samplers) return;
    es3_functions.glDeleteSamplers(n, samplers);
    for (GLsizei i = 0; i < n; i++) {
        GLuint s = samplers[i];
        if (s == 0) continue;
        for (GLuint u = 0; u < MAX_TMUS; u++) {
            if (ctx->bound_samplers[u] == s) {
                ctx->bound_samplers[u] = 0;
            }
        }
    }
}

void glBindVertexArray(GLuint array) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (ctx->current_vao_valid && ctx->current_vao == array) {
        return;
    }
    es3_functions.glBindVertexArray(array);
    ctx->current_vao = array;
    ctx->current_vao_valid = true;
}

void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !arrays) return;
    es3_functions.glDeleteVertexArrays(n, arrays);
    for (GLsizei i = 0; i < n; i++) {
        GLuint v = arrays[i];
        if (v == 0) continue;
        vao_set_ebo(ctx, v, 0);
        if (v < EMESIS_FAST_VAO_MAX) {
            ctx->vao_ebo_valid_fast[v] = 0;
            ctx->vao_vb_valid_fast[v] = 0;
        }
        if (ctx->current_vao == v) {
            ctx->current_vao = 0;
            ctx->current_vao_valid = false;
        }
    }
}

/* --- GL_ARB_multi_bind --- */
void glBindBuffersBase(GLenum target, GLuint first, GLsizei count, const GLuint *buffers) {
    if (count <= 0) return;
    for (GLsizei i = 0; i < count; i++) {
        glBindBufferBase(target, first + (GLuint)i, buffers ? buffers[i] : 0);
    }
}

void glBindBuffersRange(GLenum target, GLuint first, GLsizei count,
                        const GLuint *buffers, const GLintptr *offsets, const GLsizeiptr *sizes) {
    if (count <= 0) return;
    for (GLsizei i = 0; i < count; i++) {
        GLuint buf = buffers ? buffers[i] : 0;
        if (buf == 0) {
            glBindBufferBase(target, first + (GLuint)i, 0);
        } else {
            GLintptr off = offsets ? offsets[i] : 0;
            GLsizeiptr sz = sizes ? sizes[i] : 0;
            glBindBufferRange(target, first + (GLuint)i, buf, off, sz);
        }
    }
}

void glBindTextures(GLuint first, GLsizei count, const GLuint *textures) {
    context_t* ctx = emesis_get_context();
    if (!ctx || count <= 0) return;
    GLuint orig_unit = ctx->active_tex_unit_valid ? ctx->active_tex_unit : 0;
    GLuint cur_unit = orig_unit;
    bool cur_unit_known = ctx->active_tex_unit_valid;

    for (GLsizei i = 0; i < count; i++) {
        GLuint u = first + (GLuint)i;
        if (u >= MAX_TMUS) break;
        GLuint tex = textures ? textures[i] : 0;
        if (tex == 0) {
            for (int t = 0; t < MAX_TEXTARGETS; t++) {
                uint16_t bit = (uint16_t)(1u << t);
                if ((ctx->bound_textures_valid[u] & bit) != 0 && ctx->bound_textures[u][t] == 0) {
                    continue;
                }
                if ((ctx->bound_textures_valid[u] & bit) == 0 && t != 0) {
                    continue;
                }
                if (!cur_unit_known || cur_unit != u) {
                    es3_functions.glActiveTexture(GL_TEXTURE0 + u);
                    cur_unit = u;
                    cur_unit_known = true;
                }
                es3_functions.glBindTexture(emesis_tex_index_to_target(t), 0);
                ctx->bound_textures[u][t] = 0;
                ctx->bound_textures_valid[u] |= bit;
            }
        } else {
            int t_idx = 0;
            if (tex < EMESIS_FAST_TEX_MAX && ctx->tex_target_fast[tex] != 0) {
                t_idx = (int)ctx->tex_target_fast[tex] - 1;
            }
            uint16_t bit = (uint16_t)(1u << t_idx);
            if ((ctx->bound_textures_valid[u] & bit) != 0 && ctx->bound_textures[u][t_idx] == tex) {
                continue;
            }
            if (!cur_unit_known || cur_unit != u) {
                es3_functions.glActiveTexture(GL_TEXTURE0 + u);
                cur_unit = u;
                cur_unit_known = true;
            }
            es3_functions.glBindTexture(emesis_tex_index_to_target(t_idx), tex);
            ctx->bound_textures[u][t_idx] = tex;
            ctx->bound_textures_valid[u] |= bit;
        }
    }

    if (cur_unit_known && cur_unit != orig_unit) {
        es3_functions.glActiveTexture(GL_TEXTURE0 + orig_unit);
        ctx->active_tex_unit = orig_unit;
        ctx->active_tex_unit_valid = true;
    } else if (cur_unit_known) {
        ctx->active_tex_unit = cur_unit;
        ctx->active_tex_unit_valid = true;
    }
}

void glBindSamplers(GLuint first, GLsizei count, const GLuint *samplers) {
    if (count <= 0) return;
    for (GLsizei i = 0; i < count; i++) {
        glBindSampler(first + (GLuint)i, samplers ? samplers[i] : 0);
    }
}

void glBindImageTextures(GLuint first, GLsizei count, const GLuint *textures) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !ctx->es31 || !es3_functions.glBindImageTexture || count <= 0) return;
    for (GLsizei i = 0; i < count; i++) {
        GLuint unit = first + (GLuint)i;
        GLuint tex = textures ? textures[i] : 0;
        if (tex == 0) {
            es3_functions.glBindImageTexture(unit, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R8);
        } else {
            GLenum fmt = (tex < EMESIS_FAST_TEX_MAX && ctx->tex_intfmt_fast[tex] != 0)
                ? ctx->tex_intfmt_fast[tex]
                : GL_RGBA8;
            es3_functions.glBindImageTexture(unit, tex, 0, GL_TRUE, 0, GL_READ_WRITE, fmt);
        }
    }
}

void glBindVertexBuffers(GLuint first, GLsizei count, const GLuint *buffers,
                         const GLintptr *offsets, const GLsizei *strides) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !ctx->es31 || !es3_functions.glBindVertexBuffer || count <= 0) return;
    for (GLsizei i = 0; i < count; i++) {
        GLuint slot = first + (GLuint)i;
        if (!buffers || buffers[i] == 0) {
            glBindVertexBuffer(slot, 0, 0, 16);
        } else {
            GLintptr off = offsets ? offsets[i] : 0;
            GLsizei str = strides ? strides[i] : 16;
            glBindVertexBuffer(slot, buffers[i], off, str);
        }
    }
}

static inline int nlevel(int size, int level) {
    if (size) {
        size >>= level;
        if (!size) size = 1;
    }
    return size;
}

static bool trigger_texlevelparameter = false;

static bool check_texlevelparameter(context_t* ctx) {
    if (ctx->es31 && es3_functions.glGetTexLevelParameteriv) return true;
    if (trigger_texlevelparameter) return false;
    printf("EmesisRenderer: glGetTexLevelParameter* functions are not supported below OpenGL ES 3.1\n");
    trigger_texlevelparameter = true;
    return false;
}

static void proxy_getlevelparameter(context_t* ctx, GLenum target, GLint level, GLenum pname, GLint *params) {
    (void)target;
    switch (pname) {
        case GL_TEXTURE_WIDTH:
            (*params) = nlevel(ctx->proxy_width, level);
            break;
        case GL_TEXTURE_HEIGHT:
            (*params) = nlevel(ctx->proxy_height, level);
            break;
        case GL_TEXTURE_INTERNAL_FORMAT:
            (*params) = ctx->proxy_intformat;
            break;
        default:
            (*params) = 0;
            break;
    }
}

void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (isProxyTexture(target)) {
        GLint param = 0;
        proxy_getlevelparameter(ctx, target, level, pname, &param);
        *params = (GLfloat)param;
        return;
    }
    if (!check_texlevelparameter(ctx)) return;
    es3_functions.glGetTexLevelParameterfv(target, level, pname, params);
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (isProxyTexture(target)) {
        proxy_getlevelparameter(ctx, target, level, pname, params);
        return;
    }
    if (!check_texlevelparameter(ctx)) return;
    es3_functions.glGetTexLevelParameteriv(target, level, pname, params);
}

void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                  GLint border, GLenum format, GLenum type, const GLvoid *data) {
    if (isProxyTexture(target)) {
        glTexImage2D(GL_PROXY_TEXTURE_2D, level, internalformat, width, 1, border, format, type, data);
    } else {
        glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, data);
    }
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *data) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (isProxyTexture(target)) {
        ctx->proxy_width = ((width << level) > ctx->maxTextureSize) ? 0 : width;
        ctx->proxy_height = ((height << level) > ctx->maxTextureSize) ? 0 : height;
        ctx->proxy_intformat = internalformat;
    } else {
        if (data != NULL) swizzle_process_upload(target, &format, &type);
        pick_internalformat(&internalformat, &type, &format, &data);
        record_tex_format_fast(ctx, target, (GLenum)internalformat, format);
        es3_functions.glTexImage2D(target, level, internalformat, width, height, border, format, type, data);
    }
}

void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth,
                  GLint border, GLenum format, GLenum type, const GLvoid *data) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (isProxyTexture(target)) {
        ctx->proxy_width = ((width << level) > ctx->maxTextureSize) ? 0 : width;
        ctx->proxy_height = ((height << level) > ctx->maxTextureSize) ? 0 : height;
        ctx->proxy_intformat = internalformat;
        return;
    }
    if (data != NULL) swizzle_process_upload(target, &format, &type);
    pick_internalformat(&internalformat, &type, &format, &data);
    record_tex_format_fast(ctx, target, (GLenum)internalformat, format);
    es3_functions.glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, data);
}

void glTexStorage1D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width) {
    if (isProxyTexture(target)) {
        glTexStorage2D(GL_PROXY_TEXTURE_2D, levels, internalformat, width, 1);
    } else {
        glTexStorage2D(GL_TEXTURE_2D, levels, internalformat, width, 1);
    }
}

void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (isProxyTexture(target)) {
        ctx->proxy_width = (width > ctx->maxTextureSize) ? 0 : width;
        ctx->proxy_height = (height > ctx->maxTextureSize) ? 0 : height;
        ctx->proxy_intformat = (GLint)internalformat;
        return;
    }
    internalformat = remap_tex_storage_internalformat(internalformat);
    record_tex_format_fast(ctx, target, internalformat, internalformat);
    es3_functions.glTexStorage2D(target, levels, internalformat, width, height);
}

void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    internalformat = remap_tex_storage_internalformat(internalformat);
    record_tex_format_fast(ctx, target, internalformat, internalformat);
    es3_functions.glTexStorage3D(target, levels, internalformat, width, height, depth);
}

INTERNAL bool filter_params_integer(GLenum target, GLenum pname, GLint param) {
    (void)target; (void)pname; (void)param;
    return true;
}

INTERNAL bool filter_params_float(GLenum target, GLenum pname, GLfloat param) {
    (void)target;
    if (pname == GL_TEXTURE_LOD_BIAS) {
        if (param != 0.0f) {
            static bool lodbias_trigger = false;
            if (!lodbias_trigger) {
                lodbias_trigger = true;
                printf("EmesisRenderer: setting GL_TEXTURE_LOD_BIAS to nondefault value not supported\n");
            }
        }
        return false;
    }
    return true;
}

static bool try_dedup_tex_mip_range(context_t* ctx, GLenum target, GLenum pname, GLint param) {
    if ((pname != GL_TEXTURE_BASE_LEVEL && pname != GL_TEXTURE_MAX_LEVEL) ||
        param < 0 || param > 30000) {
        return false;
    }
    GLuint tex = emesis_get_bound_texture(target);
    if (tex == 0 || tex >= EMESIS_FAST_TEX_MAX) return false;
    int16_t encoded = (int16_t)(param + 1);
    if (pname == GL_TEXTURE_BASE_LEVEL) {
        if (ctx->tex_base_level_fast[tex] == encoded) return true;
        ctx->tex_base_level_fast[tex] = encoded;
    } else {
        if (ctx->tex_max_level_fast[tex] == encoded) return true;
        ctx->tex_max_level_fast[tex] = encoded;
    }
    return false;
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (!filter_params_integer(target, pname, (GLint)param)) return;
    if (!filter_params_float(target, pname, param)) return;
    if (try_dedup_tex_mip_range(ctx, target, pname, (GLint)param)) return;
    es3_functions.glTexParameterf(target, pname, param);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (!filter_params_integer(target, pname, param)) return;
    if (!filter_params_float(target, pname, (GLfloat)param)) return;
    if (try_dedup_tex_mip_range(ctx, target, pname, param)) return;
    GLenum swz = (GLenum)param;
    swizzle_process_swizzle_param(target, pname, &swz);
    if (pname == GL_TEXTURE_SWIZZLE_R || pname == GL_TEXTURE_SWIZZLE_G ||
        pname == GL_TEXTURE_SWIZZLE_B || pname == GL_TEXTURE_SWIZZLE_A) {
        return;
    }
    es3_functions.glTexParameteri(target, pname, param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat * params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (!filter_params_integer(target, pname, (GLint)*params)) return;
    if (!filter_params_float(target, pname, *params)) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) return;
    if (try_dedup_tex_mip_range(ctx, target, pname, (GLint)*params)) return;
    es3_functions.glTexParameterfv(target, pname, params);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint * params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (!filter_params_integer(target, pname, *params)) return;
    if (!filter_params_float(target, pname, (GLfloat)*params)) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) return;
    if (try_dedup_tex_mip_range(ctx, target, pname, *params)) return;
    swizzle_process_swizzle_param(target, pname, (const GLenum*)params);
    if (pname == GL_TEXTURE_SWIZZLE_R || pname == GL_TEXTURE_SWIZZLE_G ||
        pname == GL_TEXTURE_SWIZZLE_B || pname == GL_TEXTURE_SWIZZLE_A ||
        pname == GL_TEXTURE_SWIZZLE_RGBA) {
        return;
    }
    es3_functions.glTexParameteriv(target, pname, params);
}

static bool trigger_gltexparameteri = false;
void glTexParameterIiv(GLenum target, GLenum pname, const GLint * params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (pname != GL_TEXTURE_SWIZZLE_RGBA) {
        if (!trigger_gltexparameteri) {
            printf("EmesisRenderer: glTexParameterIiv for parameters other than GL_TEXTURE_SWIZZLE_RGBA is not supported\n");
            trigger_gltexparameteri = true;
        }
        return;
    }
    swizzle_process_swizzle_param(target, pname, (const GLenum*)params);
}

void glTexParameterIuiv(GLenum target, GLenum pname, const GLuint * params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (pname != GL_TEXTURE_SWIZZLE_RGBA) {
        if (!trigger_gltexparameteri) {
            printf("EmesisRenderer: glTexParameterIuiv for parameters other than GL_TEXTURE_SWIZZLE_RGBA is not supported\n");
            trigger_gltexparameteri = true;
        }
        return;
    }
    swizzle_process_swizzle_param(target, pname, (const GLenum*)params);
}

void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    internalformat = remap_tex_storage_internalformat(internalformat);
    es3_functions.glRenderbufferStorage(target, internalformat, width, height);
}

static void invalidate_ubo_cache_for_buffer(context_t* ctx, GLuint buf) {
    if (!ctx || buf == 0) return;
    for (int i = 0; i < MAX_UBO_SLOTS; i++) {
        if (ctx->ubo_bindings[i].valid && ctx->ubo_bindings[i].buffer == buf) {
            ctx->ubo_bindings[i].valid = false;
        }
    }
}

static size_t query_device_ram_mb(void) {
    static size_t s_cached_mb = 0;
    if (s_cached_mb != 0) return s_cached_mb;
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) {
        s_cached_mb = 4096;
        return s_cached_mb;
    }
    char line[128];
    unsigned long kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %lu kB", &kb) == 1 && kb > 0) {
            break;
        }
    }
    fclose(f);
    s_cached_mb = kb ? (size_t)(kb / 1024ul) : 4096u;
    return s_cached_mb;
}

static bool upload_ring_ensure_init(context_t* ctx) {
    upload_ring_t* ring = &ctx->upload_ring;
    if (ring->ready) return true;
    if (ring->init_attempted) return false;
    ring->init_attempted = true;

    size_t total = EMESIS_UPLOAD_RING_SIZE_32;
    if (sizeof(void*) > 4) {
        size_t ram_mb = query_device_ram_mb();
        if (ram_mb <= 4608u) {
            total = 8u * 1024u * 1024u;
        } else if (ram_mb <= 6656u) {
            total = 16u * 1024u * 1024u;
        } else {
            total = EMESIS_UPLOAD_RING_SIZE_64;
        }
    }
    es3_functions.glGenBuffers(1, &ring->buffer);
    if (ring->buffer == 0) return false;

    int cr_idx = get_buffer_index(GL_COPY_READ_BUFFER);
    GLuint prev_cr = (cr_idx >= 0 && (ctx->bound_buffers_valid_mask & (1u << cr_idx)) != 0)
        ? ctx->bound_buffers[cr_idx] : 0;

    es3_functions.glBindBuffer(GL_COPY_READ_BUFFER, ring->buffer);
    if (cr_idx >= 0) {
        ctx->bound_buffers[cr_idx] = ring->buffer;
        ctx->bound_buffers_valid_mask |= (uint16_t)(1u << cr_idx);
    }

    ring->total_size = total;
    ring->seg_size = total / EMESIS_UPLOAD_SEGMENTS;
    ring->cur_offset = 0;
    ring->mapped_ptr = NULL;

    if (ctx->buffer_storage && es3_functions.glBufferStorageEXT) {
        GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT | GL_MAP_COHERENT_BIT_EXT;
        es3_functions.glBufferStorageEXT(GL_COPY_READ_BUFFER, (GLsizeiptr)total, NULL, flags);
        void* mapped = es3_functions.glMapBufferRange(GL_COPY_READ_BUFFER, 0, (GLsizeiptr)total, flags);
        if (mapped != NULL) {
            ring->mapped_ptr = mapped;
        }
    }
    if (ring->mapped_ptr == NULL) {
        es3_functions.glBufferData(GL_COPY_READ_BUFFER, (GLsizeiptr)total, NULL, GL_STREAM_COPY);
    }

    if (prev_cr != 0 && prev_cr != ring->buffer) {
        es3_functions.glBindBuffer(GL_COPY_READ_BUFFER, prev_cr);
        if (cr_idx >= 0) {
            ctx->bound_buffers[cr_idx] = prev_cr;
        }
    }
    ring->ready = true;
    return true;
}

static bool upload_ring_subdata(context_t* ctx, GLenum target, GLuint dst_buf, size_t dst_buf_size,
                                GLintptr offset, GLsizeiptr size, const void* data) {
    if (!use_upload_ring || target == GL_COPY_READ_BUFFER || dst_buf == 0 ||
        dst_buf_size < EMESIS_UPLOAD_RING_MIN_DST_SIZE || offset < 0 || size <= 0 || !data ||
        (size_t)offset + (size_t)size > dst_buf_size) {
        return false;
    }
    if (!upload_ring_ensure_init(ctx)) return false;
    upload_ring_t* ring = &ctx->upload_ring;
    if (dst_buf == ring->buffer || (size_t)size > ring->seg_size) return false;

    size_t aligned_sz = ((size_t)size + 15u) & ~15u;
    size_t cur_seg = ring->cur_offset / ring->seg_size;
    size_t seg_end = (cur_seg + 1u) * ring->seg_size;

    if (ring->cur_offset + aligned_sz > seg_end) {
        if (ring->fences[cur_seg] != NULL) {
            es3_functions.glDeleteSync(ring->fences[cur_seg]);
        }
        ring->fences[cur_seg] = es3_functions.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        size_t next_seg = (cur_seg + 1u) % EMESIS_UPLOAD_SEGMENTS;
        if (ring->fences[next_seg] != NULL) {
            GLenum w = es3_functions.glClientWaitSync(ring->fences[next_seg], 0, 0);
            if (w == GL_TIMEOUT_EXPIRED) {
                es3_functions.glClientWaitSync(ring->fences[next_seg], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL);
            }
            es3_functions.glDeleteSync(ring->fences[next_seg]);
            ring->fences[next_seg] = NULL;
        }
        ring->cur_offset = next_seg * ring->seg_size;
    }

    size_t src_off = ring->cur_offset;
    glBindBuffer(GL_COPY_READ_BUFFER, ring->buffer);

    if (ring->mapped_ptr != NULL) {
        memcpy((uint8_t*)ring->mapped_ptr + src_off, data, (size_t)size);
    } else {
        void* dst = es3_functions.glMapBufferRange(GL_COPY_READ_BUFFER, (GLintptr)src_off, size,
                                                   GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (!dst) return false;
        memcpy(dst, data, (size_t)size);
        es3_functions.glUnmapBuffer(GL_COPY_READ_BUFFER);
    }

    es3_functions.glCopyBufferSubData(GL_COPY_READ_BUFFER, target, (GLintptr)src_off, offset, size);
    ring->cur_offset = src_off + aligned_sz;
    return true;
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    context_t* ctx = emesis_get_context();
    if (!ctx || target == GL_QUERY_BUFFER) return;
    GLuint buf_id = get_bound_buffer_for_target(ctx, target);
    if (buf_id != 0) {
        buf_set_coherent(ctx, buf_id, false);
        buf_set_size(ctx, buf_id, (size > 0) ? (size_t)size : 0u);
        buf_invalidate_tbo_bindings(ctx, buf_id);
        invalidate_ubo_cache_for_buffer(ctx, buf_id);
    }
    es3_functions.glBufferData(target, size, data, usage);
    if (__builtin_expect(ctx->es30_needs_ebo_mirror, 0)) {
        emesis_es30_on_buffer_data(ctx, target, size, data);
    }
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || target == GL_QUERY_BUFFER || size <= 0 || !data) return;
    GLuint buf_id = get_bound_buffer_for_target(ctx, target);
    size_t bsz = buf_get_size(ctx, buf_id);
    if (!upload_ring_subdata(ctx, target, buf_id, bsz, offset, size, data)) {
        es3_functions.glBufferSubData(target, offset, size, data);
    }
    if (__builtin_expect(ctx->es30_needs_ebo_mirror, 0)) {
        emesis_es30_on_buffer_sub_data(ctx, target, offset, size, data);
    }
}

void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
    context_t* ctx = emesis_get_context();
    if (!ctx || readTarget == GL_QUERY_BUFFER || writeTarget == GL_QUERY_BUFFER) return;
    es3_functions.glCopyBufferSubData(readTarget, writeTarget, readOffset, writeOffset, size);
    if (__builtin_expect(ctx->es30_needs_ebo_mirror, 0)) {
        emesis_es30_on_copy_buffer_sub_data(ctx, readTarget, writeTarget, readOffset, writeOffset, size);
    }
}

#define EMESIS_32BIT_MAX_SINGLE_PERSISTENT (96u * 1024u * 1024u)

void glBufferStorage(GLenum target, GLsizeiptr size, const void * data, GLbitfield flags) {
    context_t* ctx = emesis_get_context();
    if (!ctx || target == GL_QUERY_BUFFER) return;
    GLuint buf_id = get_bound_buffer_for_target(ctx, target);
    if (buf_id != 0) {
        buf_set_coherent(ctx, buf_id, false);
        buf_set_size(ctx, buf_id, (size > 0) ? (size_t)size : 0u);
        buf_invalidate_tbo_bindings(ctx, buf_id);
        invalidate_ubo_cache_for_buffer(ctx, buf_id);
    }

    bool force_emulate_32bit = (sizeof(void*) == 4 &&
                                (flags & GL_MAP_PERSISTENT_BIT) != 0 &&
                                size > (GLsizeiptr)EMESIS_32BIT_MAX_SINGLE_PERSISTENT);

    if (!ctx->buffer_storage || !es3_functions.glBufferStorageEXT || force_emulate_32bit) {
        GLenum usage = (flags & (GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT))
            ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        es3_functions.glBufferData(target, size, data, usage);
    } else {
        if (never_flush_buffers && ((flags & GL_MAP_PERSISTENT_BIT) != 0)) {
            flags |= GL_MAP_COHERENT_BIT;
        }
        if (coherent_dynamic_storage && sizeof(void*) > 4 &&
            (flags & (GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT)) == (GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT)) {
            flags |= (GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
        }
        es3_functions.glBufferStorageEXT(target, size, data, flags);
    }
    if (__builtin_expect(ctx->es30_needs_ebo_mirror, 0)) {
        emesis_es30_on_buffer_data(ctx, target, size, data);
    }
}

void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    context_t* ctx = emesis_get_context();
    if (!ctx || target == GL_QUERY_BUFFER) return NULL;
    GLuint buf_id = get_bound_buffer_for_target(ctx, target);

    if (!ctx->buffer_storage || !es3_functions.glBufferStorageEXT) {
        access &= ~(GLbitfield)(GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    } else if ((access & GL_MAP_PERSISTENT_BIT) != 0 && never_flush_buffers) {
        access = (access | GL_MAP_COHERENT_BIT) & ~(GLbitfield)GL_MAP_FLUSH_EXPLICIT_BIT;
    }

    bool coherent_now = (access & GL_MAP_COHERENT_BIT) != 0 && (access & GL_MAP_FLUSH_EXPLICIT_BIT) == 0;
    if (buf_id != 0) {
        buf_set_coherent(ctx, buf_id, coherent_now);
        if (__builtin_expect(ctx->es30_needs_ebo_mirror, 0) && (access & GL_MAP_WRITE_BIT) != 0) {
            emesis_es30_invalidate_buffer(ctx, buf_id);
        }
    }
    return es3_functions.glMapBufferRange(target, offset, length, access);
}

void *glMapBuffer(GLenum target, GLenum access) {
    context_t* ctx = emesis_get_context();
    if (!ctx || target == GL_QUERY_BUFFER) return NULL;

    GLbitfield access_range = 0;
    switch (access) {
        case GL_READ_ONLY:
            access_range = GL_MAP_READ_BIT;
            break;
        case GL_WRITE_ONLY:
            access_range = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT;
            break;
        case GL_READ_WRITE:
            access_range = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
            break;
        default:
            return NULL;
    }

    GLint64 length64 = 0;
    if (es3_functions.glGetBufferParameteri64v) {
        es3_functions.glGetBufferParameteri64v(target, GL_BUFFER_SIZE, &length64);
    } else {
        GLint length32 = 0;
        es3_functions.glGetBufferParameteriv(target, GL_BUFFER_SIZE, &length32);
        length64 = length32;
    }
    if (length64 <= 0) return NULL;
    return glMapBufferRange(target, 0, (GLsizeiptr)length64, access_range);
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    context_t* ctx = emesis_get_context();
    if (!ctx || target == GL_QUERY_BUFFER) return;
    GLuint buf_id = get_bound_buffer_for_target(ctx, target);
    if (buf_id != 0 && buf_is_coherent(ctx, buf_id)) {
        return;
    }
    es3_functions.glFlushMappedBufferRange(target, offset, length);
}

GLboolean glUnmapBuffer(GLenum target) {
    context_t* ctx = emesis_get_context();
    if (!ctx || target == GL_QUERY_BUFFER) return GL_FALSE;
    GLuint buf_id = get_bound_buffer_for_target(ctx, target);
    if (buf_id != 0) {
        buf_set_coherent(ctx, buf_id, false);
    }
    return es3_functions.glUnmapBuffer(target);
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
    context_t* ctx = emesis_get_context();
    if (!ctx || name != GL_EXTENSIONS) return NULL;
    if (index >= ctx->nextras) {
        return es3_functions.glGetStringi(name, index - (GLuint)ctx->nextras);
    } else {
        return (const GLubyte*)ctx->extra_extensions_array[index];
    }
}

#define GL_VERSION_STRING "%u.%u EmesisRenderer-Modern"
static char gl_version_buf[64];

const GLubyte* glGetString(GLenum name) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return NULL;
    switch (name) {
        case GL_VERSION:
            if (!gl_version_buf[0]) {
                snprintf(gl_version_buf, sizeof(gl_version_buf), GL_VERSION_STRING, gl_major, gl_minor);
            }
            return (const GLubyte*)gl_version_buf;
        case GL_SHADING_LANGUAGE_VERSION:
            return (const GLubyte*)"4.60 Emesis Renderer";
        case GL_VENDOR:
            return (const GLubyte*)"Entitybrian";
        case GL_EXTENSIONS:
            if (ctx->extensions_string != NULL) return (const GLubyte*)ctx->extensions_string;
            return (const GLubyte*)es3_functions.glGetString(GL_EXTENSIONS);
        default:
            return es3_functions.glGetString(name);
    }
}

static bool is_valid_es_capability(GLenum cap, bool es32) {
    switch (cap) {
        case GL_BLEND:
        case GL_CULL_FACE:
        case GL_DEPTH_TEST:
        case GL_DITHER:
        case GL_POLYGON_OFFSET_FILL:
        case GL_PRIMITIVE_RESTART_FIXED_INDEX:
        case GL_RASTERIZER_DISCARD:
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
        case GL_SAMPLE_COVERAGE:
        case GL_SAMPLE_MASK:
        case GL_SCISSOR_TEST:
        case GL_STENCIL_TEST:
            return true;
        case GL_DEBUG_OUTPUT:
        case GL_DEBUG_OUTPUT_SYNCHRONOUS:
            return es32 && debug;
        default:
            return false;
    }
}

void glEnable(GLenum cap) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !is_valid_es_capability(cap, ctx->es32)) return;
    es3_functions.glEnable(cap);
}

void glDisable(GLenum cap) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !is_valid_es_capability(cap, ctx->es32)) return;
    es3_functions.glDisable(cap);
}

GLboolean glIsEnabled(GLenum cap) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !is_valid_es_capability(cap, ctx->es32)) return GL_FALSE;
    return es3_functions.glIsEnabled(cap);
}

void glPolygonMode(GLenum face, GLenum mode) {
    (void)face; (void)mode;
}

void glBindBuffer(GLenum buffer, GLuint name) {
    context_t* ctx = emesis_get_context();
    if (!ctx || buffer == GL_QUERY_BUFFER) return;
    if (buffer == GL_ELEMENT_ARRAY_BUFFER) {
        if (ctx->current_vao_valid && vao_is_ebo_cached(ctx, ctx->current_vao, name)) {
            return;
        }
        es3_functions.glBindBuffer(buffer, name);
        vao_set_ebo(ctx, ctx->current_vao, name);
        return;
    }
    int buffer_index = get_buffer_index(buffer);
    if (buffer_index >= 0) {
        uint16_t bit = (uint16_t)(1u << buffer_index);
        if ((ctx->bound_buffers_valid_mask & bit) != 0 && ctx->bound_buffers[buffer_index] == name) {
            return;
        }
        es3_functions.glBindBuffer(buffer, name);
        ctx->bound_buffers[buffer_index] = name;
        ctx->bound_buffers_valid_mask |= bit;
        return;
    }
    es3_functions.glBindBuffer(buffer, name);
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !buffers) return;
    es3_functions.glDeleteBuffers(n, buffers);
    bool clear_vb_cache = false;
    for (GLsizei i = 0; i < n; i++) {
        GLuint b = buffers[i];
        if (b == 0) continue;
        if (b >= EMESIS_FAST_BUF_MAX || (ctx->buf_flags_fast[b] & EMESIS_BUF_FLAG_VB_BOUND) != 0) {
            clear_vb_cache = true;
            if (b < EMESIS_FAST_BUF_MAX) {
                ctx->buf_flags_fast[b] &= (uint8_t)~EMESIS_BUF_FLAG_VB_BOUND;
            }
        }
        buf_set_coherent(ctx, b, false);
        buf_set_size(ctx, b, 0);
        buf_invalidate_tbo_bindings(ctx, b);
        if (__builtin_expect(ctx->es30_needs_ebo_mirror, 0)) {
            emesis_es30_delete_buffer(ctx, b);
        }
        for (int j = 0; j < MAX_BOUND_BUFFERS; j++) {
            if (ctx->bound_buffers[j] == b) {
                ctx->bound_buffers[j] = 0;
            }
        }
        invalidate_ubo_cache_for_buffer(ctx, b);
        if (ctx->default_vao_ebo == b) {
            ctx->default_vao_ebo = 0;
            ctx->default_vao_ebo_valid = false;
        }
        if (ctx->current_vao != 0 && vao_get_ebo(ctx, ctx->current_vao) == b) {
            vao_set_ebo(ctx, ctx->current_vao, 0);
            if (ctx->current_vao < EMESIS_FAST_VAO_MAX) {
                ctx->vao_ebo_valid_fast[ctx->current_vao] = 0;
            }
        }
    }
    if (clear_vb_cache) {
        memset(ctx->vao_vb_valid_fast, 0, sizeof(ctx->vao_vb_valid_fast));
    }
}

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    context_t* ctx = emesis_get_context();
    if (!ctx || target == GL_QUERY_BUFFER) return;
    int generic_idx = get_buffer_index(target);
    if (target == GL_UNIFORM_BUFFER && index < MAX_UBO_SLOTS) {
        ubo_slot_binding_t* slot = &ctx->ubo_bindings[index];
        if (slot->valid && !slot->ranged && slot->buffer == buffer &&
            generic_idx >= 0 && (ctx->bound_buffers_valid_mask & (1u << generic_idx)) != 0 &&
            ctx->bound_buffers[generic_idx] == buffer) {
            return;
        }
        slot->valid = true;
        slot->ranged = false;
        slot->buffer = buffer;
        slot->offset = 0;
        slot->size = 0;
    }
    if (generic_idx >= 0) {
        ctx->bound_buffers[generic_idx] = buffer;
        ctx->bound_buffers_valid_mask |= (uint16_t)(1u << generic_idx);
    }
    es3_functions.glBindBufferBase(target, index, buffer);
}

void glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    context_t* ctx = emesis_get_context();
    if (!ctx || target == GL_QUERY_BUFFER) return;
    int generic_idx = get_buffer_index(target);
    if (target == GL_UNIFORM_BUFFER && index < MAX_UBO_SLOTS) {
        ubo_slot_binding_t* slot = &ctx->ubo_bindings[index];
        if (slot->valid && slot->ranged && slot->buffer == buffer &&
            slot->offset == offset && slot->size == size &&
            generic_idx >= 0 && (ctx->bound_buffers_valid_mask & (1u << generic_idx)) != 0 &&
            ctx->bound_buffers[generic_idx] == buffer) {
            return;
        }
        slot->valid = true;
        slot->ranged = true;
        slot->buffer = buffer;
        slot->offset = offset;
        slot->size = size;
    }
    if (generic_idx >= 0) {
        ctx->bound_buffers[generic_idx] = buffer;
        ctx->bound_buffers_valid_mask |= (uint16_t)(1u << generic_idx);
    }
    if (buffer == 0 && offset == 0 && size <= 0) {
        es3_functions.glBindBufferBase(target, index, 0);
    } else {
        es3_functions.glBindBufferRange(target, index, buffer, offset, size);
    }
}

void glUseProgram(GLuint program) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (ctx->program == program) return;
    es3_functions.glUseProgram(program);
    ctx->program = program;
}

void glGetIntegerv(GLenum pname, GLint* data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !data) return;
    switch (pname) {
        case GL_MAJOR_VERSION:
            *data = (GLint)gl_major;
            return;
        case GL_MINOR_VERSION:
            *data = (GLint)gl_minor;
            return;
        case GL_NUM_EXTENSIONS:
            es3_functions.glGetIntegerv(pname, data);
            (*data) += (GLint)ctx->nextras;
            return;
        case GL_MAX_COLOR_ATTACHMENTS:
            *data = MAX_FBTARGETS;
            return;
        case GL_MAX_DRAW_BUFFERS:
            *data = ctx->max_drawbuffers;
            return;
        case GL_CONTEXT_FLAGS:
            *data = GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT;
            return;
        case GL_CONTEXT_PROFILE_MASK:
            *data = GL_CONTEXT_CORE_PROFILE_BIT;
            return;
        default:
            es3_functions.glGetIntegerv(pname, data);
    }
}

void glGetInteger64v(GLenum pname, GLint64* data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !data) return;
    if (pname == GL_TIMESTAMP) {
        if (ctx->timer_query) {
            es3_functions.glGetInteger64v(pname, data);
        } else {
            struct timespec ts;
            if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
                *data = (GLint64)ts.tv_sec * 1000000000LL + (GLint64)ts.tv_nsec;
            } else {
                *data = 1;
            }
        }
        return;
    }
    es3_functions.glGetInteger64v(pname, data);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (ctx->viewport_valid &&
        ctx->viewport[0] == x && ctx->viewport[1] == y &&
        ctx->viewport[2] == width && ctx->viewport[3] == height) {
        return;
    }
    es3_functions.glViewport(x, y, width, height);
    ctx->viewport[0] = x;
    ctx->viewport[1] = y;
    ctx->viewport[2] = width;
    ctx->viewport[3] = height;
    ctx->viewport_valid = true;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (ctx->scissor_valid &&
        ctx->scissor[0] == x && ctx->scissor[1] == y &&
        ctx->scissor[2] == width && ctx->scissor[3] == height) {
        return;
    }
    es3_functions.glScissor(x, y, width, height);
    ctx->scissor[0] = x;
    ctx->scissor[1] = y;
    ctx->scissor[2] = width;
    ctx->scissor[3] = height;
    ctx->scissor_valid = true;
}

void glPixelStorei(GLenum pname, GLint param) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    switch (pname) {
        case GL_UNPACK_ROW_LENGTH:
            if ((ctx->unpack_valid_mask & 0x01u) && ctx->unpack_row_length == param) return;
            ctx->unpack_row_length = param;
            ctx->unpack_valid_mask |= 0x01u;
            break;
        case GL_UNPACK_IMAGE_HEIGHT:
            if ((ctx->unpack_valid_mask & 0x02u) && ctx->unpack_image_height == param) return;
            ctx->unpack_image_height = param;
            ctx->unpack_valid_mask |= 0x02u;
            break;
        case GL_UNPACK_SKIP_PIXELS:
            if ((ctx->unpack_valid_mask & 0x04u) && ctx->unpack_skip_pixels == param) return;
            ctx->unpack_skip_pixels = param;
            ctx->unpack_valid_mask |= 0x04u;
            break;
        case GL_UNPACK_SKIP_ROWS:
            if ((ctx->unpack_valid_mask & 0x08u) && ctx->unpack_skip_rows == param) return;
            ctx->unpack_skip_rows = param;
            ctx->unpack_valid_mask |= 0x08u;
            break;
        case GL_UNPACK_SKIP_IMAGES:
            if ((ctx->unpack_valid_mask & 0x10u) && ctx->unpack_skip_images == param) return;
            ctx->unpack_skip_images = param;
            ctx->unpack_valid_mask |= 0x10u;
            break;
        case GL_UNPACK_ALIGNMENT:
            if ((ctx->unpack_valid_mask & 0x20u) && ctx->unpack_alignment == param) return;
            ctx->unpack_alignment = param;
            ctx->unpack_valid_mask |= 0x20u;
            break;
        case GL_PACK_ROW_LENGTH:
            if ((ctx->pack_valid_mask & 0x01u) && ctx->pack_row_length == param) return;
            ctx->pack_row_length = param;
            ctx->pack_valid_mask |= 0x01u;
            break;
        case GL_PACK_SKIP_PIXELS:
            if ((ctx->pack_valid_mask & 0x02u) && ctx->pack_skip_pixels == param) return;
            ctx->pack_skip_pixels = param;
            ctx->pack_valid_mask |= 0x02u;
            break;
        case GL_PACK_SKIP_ROWS:
            if ((ctx->pack_valid_mask & 0x04u) && ctx->pack_skip_rows == param) return;
            ctx->pack_skip_rows = param;
            ctx->pack_valid_mask |= 0x04u;
            break;
        case GL_PACK_ALIGNMENT:
            if ((ctx->pack_valid_mask & 0x08u) && ctx->pack_alignment == param) return;
            ctx->pack_alignment = param;
            ctx->pack_valid_mask |= 0x08u;
            break;
        default:
            break;
    }
    es3_functions.glPixelStorei(pname, param);
}

void glClipControl(GLenum origin, GLenum depth) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !ctx->clip_control || !es3_functions.glClipControlEXT) return;
    es3_functions.glClipControlEXT(origin, depth);
}

void glDepthRange(GLdouble nearVal, GLdouble farVal) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    es3_functions.glDepthRangef((GLfloat)nearVal, (GLfloat)farVal);
}

void glDeleteTextures(GLsizei n, const GLuint *textures) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !textures) return;
    es3_functions.glDeleteTextures(n, textures);
    emesis_fbo_on_delete_textures(ctx, n, textures);
    for (GLsizei i = 0; i < n; i++) {
        GLuint tex = textures[i];
        if (tex == 0) continue;
        if (tex < EMESIS_FAST_TEX_MAX) {
            ctx->tex_flags_fast[tex] = 0;
            ctx->tex_target_fast[tex] = 0;
            ctx->tex_intfmt_fast[tex] = 0;
            ctx->tex_base_level_fast[tex] = 0;
            ctx->tex_max_level_fast[tex] = 0;
            ctx->tex_tbo_buf_fast[tex] = 0;
            ctx->tex_tbo_fmt_fast[tex] = 0;
        }
        void* tracker = unordered_map_remove(ctx->texture_swztrack_map, (void*)(uintptr_t)tex);
        free(tracker);
        for (GLuint u = 0; u < MAX_TMUS; u++) {
            for (int t = 0; t < MAX_TEXTARGETS; t++) {
                if (ctx->bound_textures[u][t] == tex) {
                    ctx->bound_textures[u][t] = 0;
                }
            }
        }
    }
}

static bool buf_tex_trigger = false;

void glTexBuffer(GLenum target, GLenum internalFormat, GLuint buffer) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    GLuint tex = 0;
    if (target == GL_TEXTURE_BUFFER) {
        tex = emesis_get_bound_texture(GL_TEXTURE_BUFFER);
        if (tex > 0 && tex < EMESIS_FAST_TEX_MAX && buffer != 0 &&
            ctx->tex_tbo_buf_fast[tex] == buffer &&
            ctx->tex_tbo_fmt_fast[tex] == internalFormat) {
            return;
        }
    }
    bool attached = false;
    if (ctx->es32 && es3_functions.glTexBuffer) {
        es3_functions.glTexBuffer(target, internalFormat, buffer);
        attached = true;
    } else if (ctx->buffer_texture_ext && es3_functions.glTexBufferEXT) {
        es3_functions.glTexBufferEXT(target, internalFormat, buffer);
        attached = true;
    } else if (ctx->buffer_texture_oes && es3_functions.glTexBufferOES) {
        es3_functions.glTexBufferOES(target, internalFormat, buffer);
        attached = true;
    } else if (!buf_tex_trigger) {
        buf_tex_trigger = true;
        printf("EmesisRenderer: Buffer textures aren't supported on your device\n");
    }
    if (attached && tex > 0 && tex < EMESIS_FAST_TEX_MAX) {
        ctx->tex_tbo_buf_fast[tex] = buffer;
        ctx->tex_tbo_fmt_fast[tex] = (buffer != 0) ? internalFormat : 0;
        if (buffer > 0 && buffer < EMESIS_FAST_BUF_MAX) {
            ctx->buf_flags_fast[buffer] |= EMESIS_BUF_FLAG_TBO_ATTACHED;
        }
    }
}

void glTexBufferARB(GLenum target, GLenum internalFormat, GLuint buffer) {
    glTexBuffer(target, internalFormat, buffer);
}

void glTexBufferRange(GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (target == GL_TEXTURE_BUFFER) {
        GLuint tex = emesis_get_bound_texture(GL_TEXTURE_BUFFER);
        if (tex > 0 && tex < EMESIS_FAST_TEX_MAX) {
            ctx->tex_tbo_buf_fast[tex] = 0;
            ctx->tex_tbo_fmt_fast[tex] = 0;
        }
    }
    if (ctx->es32 && es3_functions.glTexBufferRange) {
        es3_functions.glTexBufferRange(target, internalFormat, buffer, offset, size);
    } else if (ctx->buffer_texture_ext && es3_functions.glTexBufferRangeEXT) {
        es3_functions.glTexBufferRangeEXT(target, internalFormat, buffer, offset, size);
    } else if (ctx->buffer_texture_oes && es3_functions.glTexBufferRangeOES) {
        es3_functions.glTexBufferRangeOES(target, internalFormat, buffer, offset, size);
    } else if (!buf_tex_trigger) {
        buf_tex_trigger = true;
        printf("EmesisRenderer: Buffer textures aren't supported on your device\n");
    }
}

void glTexBufferRangeARB(GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    glTexBufferRange(target, internalFormat, buffer, offset, size);
}

void glCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ,
                        GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ,
                        GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !ctx->copyimagesubdata) return;
    ctx->copyimagesubdata(srcName, srcTarget, srcLevel, srcX, srcY, srcZ,
                          dstName, dstTarget, dstLevel, dstX, dstY, dstZ,
                          srcWidth, srcHeight, srcDepth);
}

static int clear_fmt_components(GLenum format) {
    switch (format) {
        case GL_RED:
        case GL_RED_INTEGER:
        case GL_DEPTH_COMPONENT:
        case GL_STENCIL_INDEX:
            return 1;
        case GL_RG:
        case GL_RG_INTEGER:
        case GL_DEPTH_STENCIL:
            return 2;
        case GL_RGB:
        case GL_BGR:
        case GL_RGB_INTEGER:
        case GL_BGR_INTEGER:
            return 3;
        default:
            return 4;
    }
}

static bool clear_fmt_is_integer(GLenum format) {
    switch (format) {
        case GL_RED_INTEGER:
        case GL_RG_INTEGER:
        case GL_RGB_INTEGER:
        case GL_RGBA_INTEGER:
        case GL_BGR_INTEGER:
        case GL_BGRA_INTEGER:
            return true;
        default:
            return false;
    }
}

static bool clear_type_is_signed_int(GLenum type) {
    return (type == GL_BYTE || type == GL_SHORT || type == GL_INT);
}

static bool clear_fmt_is_bgra(GLenum format) {
    return (format == GL_BGRA_EXT || format == GL_BGR ||
            format == GL_BGRA_INTEGER || format == GL_BGR_INTEGER);
}

static float half_to_float(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x03FFu;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 1;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1;
                exp--;
            }
            mant &= 0x03FFu;
            bits = sign | ((exp + 112u) << 23) | (mant << 13);
        }
    } else if (exp == 31u) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

static void unpack_clear_float(GLenum format, GLenum type, const void* data, GLfloat out[4]) {
    out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; out[3] = 0.0f;
    if (!data) return;
    int comps = clear_fmt_components(format);
    if (type == 0x8367 /* GL_UNSIGNED_INT_8_8_8_8_REV */) {
        type = GL_UNSIGNED_BYTE;
    } else if (type == 0x8035 /* GL_UNSIGNED_INT_8_8_8_8 */) {
        uint32_t packed = *(const uint32_t*)data;
        out[0] = (float)((packed >> 24) & 0xFFu) / 255.0f;
        out[1] = (float)((packed >> 16) & 0xFFu) / 255.0f;
        out[2] = (float)((packed >> 8) & 0xFFu) / 255.0f;
        out[3] = (float)(packed & 0xFFu) / 255.0f;
        if (clear_fmt_is_bgra(format)) {
            GLfloat t = out[0]; out[0] = out[2]; out[2] = t;
        }
        return;
    }
    for (int i = 0; i < comps; i++) {
        switch (type) {
            case GL_UNSIGNED_BYTE:
                out[i] = (float)((const uint8_t*)data)[i] / 255.0f;
                break;
            case GL_BYTE: {
                int8_t v = ((const int8_t*)data)[i];
                out[i] = (v < -127) ? -1.0f : ((float)v / 127.0f);
                break;
            }
            case GL_UNSIGNED_SHORT:
                out[i] = (float)((const uint16_t*)data)[i] / 65535.0f;
                break;
            case GL_SHORT: {
                int16_t v = ((const int16_t*)data)[i];
                out[i] = (v < -32767) ? -1.0f : ((float)v / 32767.0f);
                break;
            }
            case GL_UNSIGNED_INT:
                out[i] = (float)((double)((const uint32_t*)data)[i] / 4294967295.0);
                break;
            case GL_HALF_FLOAT:
                out[i] = half_to_float(((const uint16_t*)data)[i]);
                break;
            case GL_FLOAT:
                out[i] = ((const float*)data)[i];
                break;
            default:
                break;
        }
    }
    if (clear_fmt_is_bgra(format)) {
        GLfloat t = out[0]; out[0] = out[2]; out[2] = t;
    }
}

static void unpack_clear_int(GLenum format, GLenum type, const void* data, GLint s_out[4], GLuint u_out[4]) {
    for (int i = 0; i < 4; i++) { s_out[i] = 0; u_out[i] = 0; }
    if (!data) return;
    int comps = clear_fmt_components(format);
    for (int i = 0; i < comps; i++) {
        switch (type) {
            case GL_BYTE: s_out[i] = ((const int8_t*)data)[i]; break;
            case GL_SHORT: s_out[i] = ((const int16_t*)data)[i]; break;
            case GL_INT: s_out[i] = ((const int32_t*)data)[i]; break;
            case GL_UNSIGNED_BYTE: u_out[i] = ((const uint8_t*)data)[i]; break;
            case GL_UNSIGNED_SHORT: u_out[i] = ((const uint16_t*)data)[i]; break;
            case GL_UNSIGNED_INT: u_out[i] = ((const uint32_t*)data)[i]; break;
            default: break;
        }
    }
    if (clear_fmt_is_bgra(format)) {
        GLint st = s_out[0]; s_out[0] = s_out[2]; s_out[2] = st;
        GLuint ut = u_out[0]; u_out[0] = u_out[2]; u_out[2] = ut;
    }
}

static void emulate_clear_tex(context_t* ctx, GLuint texture, GLint level,
                              bool sub_region, GLint xoffset, GLint yoffset, GLint zoffset,
                              GLsizei width, GLsizei height, GLsizei depth,
                              GLenum format, GLenum type, const void* data) {
    if (!ctx || texture == 0) return;
    if (sub_region && (width <= 0 || height <= 0 || depth <= 0)) return;
    if (ctx->clear_fbo == 0) {
        es3_functions.glGenFramebuffers(1, &ctx->clear_fbo);
        if (ctx->clear_fbo == 0) return;
    }

    GLboolean scissor_was_on = es3_functions.glIsEnabled(GL_SCISSOR_TEST);
    GLint prev_scissor[4] = {0, 0, 0, 0};
    if (sub_region) {
        if (ctx->scissor_valid) {
            memcpy(prev_scissor, ctx->scissor, sizeof(prev_scissor));
        } else {
            es3_functions.glGetIntegerv(GL_SCISSOR_BOX, prev_scissor);
        }
        if (!scissor_was_on) es3_functions.glEnable(GL_SCISSOR_TEST);
        es3_functions.glScissor(xoffset, yoffset, width, height);
    } else if (scissor_was_on) {
        es3_functions.glDisable(GL_SCISSOR_TEST);
    }

    GLenum attachment = GL_COLOR_ATTACHMENT0;
    if (format == GL_DEPTH_COMPONENT) {
        attachment = GL_DEPTH_ATTACHMENT;
    } else if (format == GL_DEPTH_STENCIL) {
        attachment = GL_DEPTH_STENCIL_ATTACHMENT;
    } else if (format == GL_STENCIL_INDEX) {
        attachment = GL_STENCIL_ATTACHMENT;
    }

    GLboolean prev_color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean prev_depth_mask = GL_TRUE;
    if (attachment == GL_COLOR_ATTACHMENT0) {
        es3_functions.glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);
        if (!prev_color_mask[0] || !prev_color_mask[1] || !prev_color_mask[2] || !prev_color_mask[3]) {
            es3_functions.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
    } else if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        es3_functions.glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);
        if (!prev_depth_mask) es3_functions.glDepthMask(GL_TRUE);
    }

    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->clear_fbo);
    GLenum draw_buf = (attachment == GL_COLOR_ATTACHMENT0) ? GL_COLOR_ATTACHMENT0 : GL_NONE;
    es3_functions.glDrawBuffers(1, &draw_buf);

    GLsizei layers = (sub_region && depth > 1) ? depth : 1;
    bool use_layers = (sub_region && (zoffset > 0 || depth > 1));

    for (GLsizei z = 0; z < layers; z++) {
        if (use_layers) {
            es3_functions.glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, attachment, texture, level, zoffset + z);
        } else {
            es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture, level);
        }

        if (format == GL_DEPTH_COMPONENT) {
            GLfloat dv[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            unpack_clear_float(format, type, data, dv);
            es3_functions.glClearBufferfv(GL_DEPTH, 0, &dv[0]);
        } else if (format == GL_DEPTH_STENCIL) {
            GLfloat depth_val = 0.0f;
            GLint stencil_val = 0;
            if (data) {
                if (type == GL_UNSIGNED_INT_24_8) {
                    uint32_t v = *(const uint32_t*)data;
                    depth_val = (float)((double)(v >> 8) / 16777215.0);
                    stencil_val = (GLint)(v & 0xFFu);
                } else if (type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV) {
                    depth_val = *(const float*)data;
                    stencil_val = (GLint)(((const uint32_t*)data)[1] & 0xFFu);
                }
            }
            es3_functions.glClearBufferfi(GL_DEPTH_STENCIL, 0, depth_val, stencil_val);
        } else if (format == GL_STENCIL_INDEX) {
            GLint s_val[4] = {0, 0, 0, 0};
            GLuint u_val[4] = {0, 0, 0, 0};
            unpack_clear_int(format, type, data, s_val, u_val);
            GLint sv = clear_type_is_signed_int(type) ? s_val[0] : (GLint)u_val[0];
            es3_functions.glClearBufferiv(GL_STENCIL, 0, &sv);
        } else if (clear_fmt_is_integer(format)) {
            GLint s_val[4];
            GLuint u_val[4];
            unpack_clear_int(format, type, data, s_val, u_val);
            if (clear_type_is_signed_int(type)) {
                es3_functions.glClearBufferiv(GL_COLOR, 0, s_val);
            } else {
                es3_functions.glClearBufferuiv(GL_COLOR, 0, u_val);
            }
        } else {
            GLfloat f_val[4];
            unpack_clear_float(format, type, data, f_val);
            es3_functions.glClearBufferfv(GL_COLOR, 0, f_val);
        }
    }

    if (use_layers) {
        es3_functions.glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, attachment, 0, 0, 0);
    } else {
        es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
    }
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->draw_framebuffer);

    if (attachment == GL_COLOR_ATTACHMENT0) {
        if (!prev_color_mask[0] || !prev_color_mask[1] || !prev_color_mask[2] || !prev_color_mask[3]) {
            es3_functions.glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
        }
    } else if ((attachment == GL_DEPTH_ATTACHMENT || attachment == GL_DEPTH_STENCIL_ATTACHMENT) && !prev_depth_mask) {
        es3_functions.glDepthMask(GL_FALSE);
    }

    if (sub_region) {
        es3_functions.glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
        memcpy(ctx->scissor, prev_scissor, sizeof(prev_scissor));
        ctx->scissor_valid = true;
        if (!scissor_was_on) es3_functions.glDisable(GL_SCISSOR_TEST);
    } else if (scissor_was_on) {
        es3_functions.glEnable(GL_SCISSOR_TEST);
    }
}

void glClearTexImage(GLuint texture, GLint level, GLenum format, GLenum type, const void * data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    if (ctx->clear_texture && es3_functions.glClearTexImageEXT) {
        es3_functions.glClearTexImageEXT(texture, level, format, type, data);
        return;
    }
    emulate_clear_tex(ctx, texture, level, false, 0, 0, 0, 0, 0, 1, format, type, data);
}

void glClearTexImageARB(GLuint texture, GLint level, GLenum format, GLenum type, const void * data) {
    glClearTexImage(texture, level, format, type, data);
}

void glClearTexSubImage(GLuint texture, GLint level,
                        GLint xoffset, GLint yoffset, GLint zoffset,
                        GLsizei width, GLsizei height, GLsizei depth,
                        GLenum format, GLenum type,
                        const void * data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    if (ctx->clear_texture && es3_functions.glClearTexSubImageEXT) {
        es3_functions.glClearTexSubImageEXT(texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, data);
        return;
    }
    emulate_clear_tex(ctx, texture, level, true, xoffset, yoffset, zoffset, width, height, depth, format, type, data);
}

void glClearTexSubImageARB(GLuint texture, GLint level,
                           GLint xoffset, GLint yoffset, GLint zoffset,
                           GLsizei width, GLsizei height, GLsizei depth,
                           GLenum format, GLenum type,
                           const void * data) {
    glClearTexSubImage(texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, data);
}

__attribute__((constructor)) void init_noerror(void) {
    noerror = env_istrue_d("LIBGL_NOERROR", true);
    debug = env_istrue("EMESIS_DEBUG");
    never_flush_buffers = env_istrue_d("EMESIS_NEVER_FLUSH_BUFFERS", true);
    coherent_dynamic_storage = env_istrue_d("EMESIS_COHERENT_DYNAMIC_STORAGE", false);
    use_upload_ring = env_istrue_d("EMESIS_UPLOAD_RING", true) && !env_istrue("EMESIS_DISABLE_UPLOAD_RING");
    bool version_overriden = extract_gl_version(getenv("MESA_GL_VERSION_OVERRIDE"));
    gl_version_buf[0] = '\0';
    if (version_overriden) printf("EmesisRenderer will override exposed OpenGL version to %u.%u\n", gl_major, gl_minor);
    if (coherent_dynamic_storage) printf("EmesisRenderer will force dynamic storage buffers to be coherent.\n");
    if (debug) printf("EmesisRenderer will allow GL_DEBUG_OUTPUT to be enabled.\n");
}

GLenum glGetError(void) {
    if (noerror) return 0;
    else return es3_functions.glGetError();
}

void glDebugMessageControl(GLenum source, GLenum type, GLenum severity,
                           GLsizei count, const GLuint *ids, GLboolean enabled) {
    (void)source; (void)type; (void)severity; (void)count; (void)ids; (void)enabled;
}
