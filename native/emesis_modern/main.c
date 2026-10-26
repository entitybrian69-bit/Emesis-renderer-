/**
 * Created by: artDev, DuyKhanhTran
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "GL/gl.h"
#include "GL/glext.h"
#include <GLES3/gl3.h>
#include "string_utils.h"
#include "proc.h"
#include "egl.h"
#include "glformats.h"
#include "main.h"
#include "swizzle.h"
#include "libraryinternal.h"
#include "env.h"

static unsigned int gl_major = 3;
static unsigned int gl_minor = 3;
static bool noerror = false;
static bool debug = false;
static bool never_flush_buffers = true;
static bool coherent_dynamic_storage = false;
static thread_local uint32_t s_persistent_mapped_mask = 0;

static bool extract_gl_version(const char* version) {
    if(!version) return false;
    unsigned int major = 0, minor = 0;
    if (sscanf(version, "%u.%u", &major, &minor) != 2) return false;
    if((major < 2 || major > 4) || minor > 9) {
        return false;
    }
    gl_major = major;
    gl_minor = minor;
    return true;
}

void glClearDepth(GLdouble depth) {
    emesis_ensure_current_context();
    if(!current_context) return;
    es3_functions.glClearDepthf((GLfloat) depth);
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

void *glMapBuffer(GLenum target, GLenum access) {
    emesis_ensure_current_context();
    if(!current_context) return NULL;

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
    int idx = get_buffer_index(target);
    if (idx >= 0) s_persistent_mapped_mask &= ~(1u << idx);
    return es3_functions.glMapBufferRange(target, 0, (GLsizeiptr)length64, access_range);
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
        default: return -1;
    }
}

INTERNAL GLenum get_textarget_query_param(GLenum target) {
    switch (target) {
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
    if (!current_context) return 0;
    int t_idx = emesis_tex_target_index(target);
    GLuint unit = current_context->active_tex_unit;
    if (t_idx >= 0 && unit < MAX_TMUS) {
        GLuint cached = current_context->bound_textures[unit][t_idx];
        if (cached != 0) return cached;
    }
    GLenum getter = get_textarget_query_param(target);
    if (getter == 0) return 0;
    GLint queried = 0;
    es3_functions.glGetIntegerv(getter, &queried);
    if (queried > 0 && t_idx >= 0 && unit < MAX_TMUS) {
        current_context->bound_textures[unit][t_idx] = (GLuint)queried;
    }
    return (GLuint)queried;
}

INTERNAL GLuint emesis_get_current_ebo(void) {
    if (!current_context) return 0;
    if (current_context->current_vao == 0) {
        return current_context->default_vao_ebo;
    }
    void* val = unordered_map_get(current_context->vao_ebo_map,
                                  (void*)(uintptr_t)current_context->current_vao);
    return (GLuint)(uintptr_t)val;
}

void glActiveTexture(GLenum texture) {
    emesis_ensure_current_context();
    if (!current_context) return;
    es3_functions.glActiveTexture(texture);
    if (texture >= GL_TEXTURE0 && (texture - GL_TEXTURE0) < MAX_TMUS) {
        current_context->active_tex_unit = (GLuint)(texture - GL_TEXTURE0);
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    emesis_ensure_current_context();
    if (!current_context) return;
    es3_functions.glBindTexture(target, texture);
    int t_idx = emesis_tex_target_index(target);
    if (t_idx >= 0 && current_context->active_tex_unit < MAX_TMUS) {
        current_context->bound_textures[current_context->active_tex_unit][t_idx] = texture;
    }
}

void glBindVertexArray(GLuint array) {
    emesis_ensure_current_context();
    if (!current_context) return;
    es3_functions.glBindVertexArray(array);
    current_context->current_vao = array;
}

void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    emesis_ensure_current_context();
    if (!current_context || n <= 0 || !arrays) return;
    es3_functions.glDeleteVertexArrays(n, arrays);
    for (GLsizei i = 0; i < n; i++) {
        if (arrays[i] == 0) continue;
        if (current_context->current_vao == arrays[i]) {
            current_context->current_vao = 0;
        }
        unordered_map_remove(current_context->vao_ebo_map, (void*)(uintptr_t)arrays[i]);
    }
}

static inline int nlevel(int size, int level) {
    if(size) {
        size >>= level;
        if(!size) size = 1;
    }
    return size;
}

static bool trigger_texlevelparameter = false;

static bool check_texlevelparameter(void) {
    if(current_context->es31 && es3_functions.glGetTexLevelParameteriv) return true;
    if(trigger_texlevelparameter) return false;
    printf("EmesisRenderer: glGetTexLevelParameter* functions are not supported below OpenGL ES 3.1\n");
    trigger_texlevelparameter = true;
    return false;
}

static void proxy_getlevelparameter(GLenum target, GLint level, GLenum pname, GLint *params) {
    (void)target;
    switch (pname) {
        case GL_TEXTURE_WIDTH:
            (*params) = nlevel(current_context->proxy_width, level);
            break;
        case GL_TEXTURE_HEIGHT:
            (*params) = nlevel(current_context->proxy_height, level);
            break;
        case GL_TEXTURE_INTERNAL_FORMAT:
            (*params) = current_context->proxy_intformat;
            break;
        default:
            (*params) = 0;
            break;
    }
}

void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) {
    emesis_ensure_current_context();
    if(!current_context || !params) return;
    if(isProxyTexture(target)) {
        GLint param = 0;
        proxy_getlevelparameter(target, level, pname, &param);
        *params = (GLfloat) param;
        return;
    }
    if(!check_texlevelparameter()) return;
    es3_functions.glGetTexLevelParameterfv(target, level, pname, params);
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) {
    emesis_ensure_current_context();
    if(!current_context || !params) return;
    if (isProxyTexture(target)) {
        proxy_getlevelparameter(target, level, pname, params);
        return;
    }
    if(!check_texlevelparameter()) return;
    es3_functions.glGetTexLevelParameteriv(target, level, pname, params);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data) {
    emesis_ensure_current_context();
    if(!current_context) return;
    if (isProxyTexture(target)) {
        current_context->proxy_width = ((width<<level)>current_context->maxTextureSize)?0:width;
        current_context->proxy_height = ((height<<level)>current_context->maxTextureSize)?0:height;
        current_context->proxy_intformat = internalformat;
    } else {
        if(data != NULL) swizzle_process_upload(target, &format, &type);
        pick_internalformat(&internalformat, &type, &format, &data);
        es3_functions.glTexImage2D(target, level, internalformat, width, height, border, format, type, data);
    }
}

void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) {
    emesis_ensure_current_context();
    if(!current_context) return;
    if (isProxyTexture(target)) {
        current_context->proxy_width = (width > current_context->maxTextureSize) ? 0 : width;
        current_context->proxy_height = (height > current_context->maxTextureSize) ? 0 : height;
        current_context->proxy_intformat = (GLint)internalformat;
        return;
    }
    internalformat = remap_tex_storage_internalformat(internalformat);
    es3_functions.glTexStorage2D(target, levels, internalformat, width, height);
}

void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) {
    emesis_ensure_current_context();
    if(!current_context) return;
    internalformat = remap_tex_storage_internalformat(internalformat);
    es3_functions.glTexStorage3D(target, levels, internalformat, width, height, depth);
}

INTERNAL bool filter_params_integer(GLenum target, GLenum pname, GLint param) {
    (void)target; (void)pname; (void)param;
    return true;
}

INTERNAL bool filter_params_float(GLenum target, GLenum pname, GLfloat param) {
    (void)target;
    if(pname == GL_TEXTURE_LOD_BIAS) {
        if(param != 0.0f) {
            static bool lodbias_trigger = false;
            if(!lodbias_trigger) {
                lodbias_trigger = true;
                printf("EmesisRenderer: setting GL_TEXTURE_LOD_BIAS to nondefault value not supported\n");
            }
        }
        return false;
    }
    return true;
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    emesis_ensure_current_context();
    if(!current_context) return;
    if(!filter_params_integer(target, pname, (GLint) param)) return;
    if(!filter_params_float(target, pname, param)) return;
    es3_functions.glTexParameterf(target, pname, param);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    emesis_ensure_current_context();
    if(!current_context) return;
    if(!filter_params_integer(target, pname, param)) return;
    if(!filter_params_float(target, pname, (GLfloat)param)) return;
    GLenum swz = (GLenum)param;
    swizzle_process_swizzle_param(target, pname, &swz);
    if (pname == GL_TEXTURE_SWIZZLE_R || pname == GL_TEXTURE_SWIZZLE_G ||
        pname == GL_TEXTURE_SWIZZLE_B || pname == GL_TEXTURE_SWIZZLE_A) {
        return; /* swizzle_process_swizzle_param already applied the composed swizzle */
    }
    es3_functions.glTexParameteri(target, pname, param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat * params) {
    emesis_ensure_current_context();
    if(!current_context || !params) return;
    if(!filter_params_integer(target, pname, (GLint)*params)) return;
    if(!filter_params_float(target, pname, *params)) return;
    es3_functions.glTexParameterfv(target, pname, params);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint * params) {
    emesis_ensure_current_context();
    if(!current_context || !params) return;
    if(!filter_params_integer(target, pname, *params)) return;
    if(!filter_params_float(target, pname, (GLfloat)*params)) return;
    swizzle_process_swizzle_param(target, pname, (const GLenum*)params);
    if (pname == GL_TEXTURE_SWIZZLE_R || pname == GL_TEXTURE_SWIZZLE_G ||
        pname == GL_TEXTURE_SWIZZLE_B || pname == GL_TEXTURE_SWIZZLE_A ||
        pname == 0x8E46 /* GL_TEXTURE_SWIZZLE_RGBA */) {
        return;
    }
    es3_functions.glTexParameteriv(target, pname, params);
}

static bool trigger_gltexparameteri = false;
void glTexParameterIiv(GLenum target, GLenum pname, const GLint * params) {
    emesis_ensure_current_context();
    if(!current_context || !params) return;
    if(pname != GL_TEXTURE_SWIZZLE_RGBA) {
        if(!trigger_gltexparameteri) {
            printf("EmesisRenderer: glTexParameterIiv for parameters other than GL_TEXTURE_SWIZZLE_RGBA is not supported\n");
            trigger_gltexparameteri = true;
        }
        return;
    }
    swizzle_process_swizzle_param(target, pname, (const GLenum*)params);
}

void glTexParameterIuiv(GLenum target, GLenum pname, const GLuint * params) {
    emesis_ensure_current_context();
    if(!current_context || !params) return;
    if(pname != GL_TEXTURE_SWIZZLE_RGBA) {
        if(!trigger_gltexparameteri) {
            printf("EmesisRenderer: glTexParameterIuiv for parameters other than GL_TEXTURE_SWIZZLE_RGBA is not supported\n");
            trigger_gltexparameteri = true;
        }
        return;
    }
    swizzle_process_swizzle_param(target, pname, (const GLenum*)params);
}

void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    emesis_ensure_current_context();
    if(!current_context) return;
    internalformat = remap_tex_storage_internalformat(internalformat);
    es3_functions.glRenderbufferStorage(target, internalformat, width, height);
}

static void invalidate_ubo_cache_for_buffer(GLuint buf) {
    if (!current_context || buf == 0) return;
    for (int i = 0; i < MAX_UBO_SLOTS; i++) {
        if (current_context->ubo_bindings[i].valid && current_context->ubo_bindings[i].buffer == buf) {
            current_context->ubo_bindings[i].valid = false;
        }
    }
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    emesis_ensure_current_context();
    if (!current_context) return;
    int idx = get_buffer_index(target);
    if (idx >= 0) {
        s_persistent_mapped_mask &= ~(1u << idx);
        invalidate_ubo_cache_for_buffer(current_context->bound_buffers[idx]);
    }
    es3_functions.glBufferData(target, size, data, usage);
}

void glBufferStorage(GLenum target, GLsizeiptr size, const void * data, GLbitfield flags) {
    emesis_ensure_current_context();
    if(!current_context) return;
    int idx = get_buffer_index(target);
    if (idx >= 0) {
        s_persistent_mapped_mask &= ~(1u << idx);
        invalidate_ubo_cache_for_buffer(current_context->bound_buffers[idx]);
    }
    if(!current_context->buffer_storage || !es3_functions.glBufferStorageEXT) {
        GLenum usage = (flags & (GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT)) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        es3_functions.glBufferData(target, size, data, usage);
        return;
    }
    if(never_flush_buffers && ((flags & GL_MAP_PERSISTENT_BIT) != 0)) {
        flags |= GL_MAP_COHERENT_BIT;
    }
    /* Only opt dynamic buffers into coherent storage if explicitly enabled AND never on 32-bit VA space */
    if(coherent_dynamic_storage && sizeof(void*) > 4 && (flags & (GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT)) == (GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT)) {
        flags |= (GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    }
    es3_functions.glBufferStorageEXT(target, size, data, flags);
}

void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    emesis_ensure_current_context();
    if (!current_context) return NULL;
    int idx = get_buffer_index(target);
    if ((access & GL_MAP_PERSISTENT_BIT) != 0) {
        if (never_flush_buffers) {
            access = (access | GL_MAP_COHERENT_BIT) & ~GL_MAP_FLUSH_EXPLICIT_BIT;
            if (idx >= 0) s_persistent_mapped_mask |= (1u << idx);
        }
    } else if (idx >= 0) {
        s_persistent_mapped_mask &= ~(1u << idx);
    }
    return es3_functions.glMapBufferRange(target, offset, length, access);
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    emesis_ensure_current_context();
    int idx = get_buffer_index(target);
    if (never_flush_buffers && idx >= 0 && (s_persistent_mapped_mask & (1u << idx)) != 0) {
        return;
    }
    es3_functions.glFlushMappedBufferRange(target, offset, length);
}

GLboolean glUnmapBuffer(GLenum target) {
    emesis_ensure_current_context();
    int idx = get_buffer_index(target);
    if (idx >= 0) {
        s_persistent_mapped_mask &= ~(1u << idx);
    }
    return es3_functions.glUnmapBuffer(target);
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
    emesis_ensure_current_context();
    if(!current_context || name != GL_EXTENSIONS) return NULL;
    if(index >= current_context->nextras) {
        return es3_functions.glGetStringi(name, index - (GLuint)current_context->nextras);
    } else {
        return (const GLubyte*)current_context->extra_extensions_array[index];
    }
}

#define GL_VERSION_STRING "%u.%u EmesisRenderer-Modern (Built on: " __DATE__ "/" __TIME__ ")"

static char* gl_version = NULL;

const GLubyte* glGetString(GLenum name) {
    emesis_ensure_current_context();
    if(!current_context) return NULL;
    switch(name) {
        case GL_VERSION:
            if(!gl_version) {
                if (asprintf(&gl_version, GL_VERSION_STRING, gl_major, gl_minor) == -1) {
                    gl_version = NULL;
                    return (const GLubyte*)"3.3 EmesisRenderer-Modern";
                }
            }
            return (const GLubyte*)gl_version;
        case GL_SHADING_LANGUAGE_VERSION:
            return (const GLubyte*)"4.60 Emesis Renderer";
        case GL_VENDOR:
            return (const GLubyte*)"Entitybrian";
        case GL_EXTENSIONS:
            if(current_context->extensions_string != NULL) return (const GLubyte*)current_context->extensions_string;
            return (const GLubyte*)es3_functions.glGetString(GL_EXTENSIONS);
        default:
            return es3_functions.glGetString(name);
    }
}

void glEnable(GLenum cap) {
    emesis_ensure_current_context();
    if(!current_context) return;
    if(cap == GL_DEBUG_OUTPUT && !debug) return;
    es3_functions.glEnable(cap);
}

void glBindBuffer(GLenum buffer, GLuint name) {
    emesis_ensure_current_context();
    if(!current_context) return;
    es3_functions.glBindBuffer(buffer, name);
    if (buffer == GL_ELEMENT_ARRAY_BUFFER) {
        if (current_context->current_vao == 0) {
            current_context->default_vao_ebo = name;
        } else if (name == 0) {
            unordered_map_remove(current_context->vao_ebo_map,
                                 (void*)(uintptr_t)current_context->current_vao);
        } else {
            unordered_map_put(current_context->vao_ebo_map,
                              (void*)(uintptr_t)current_context->current_vao,
                              (void*)(uintptr_t)name);
        }
        return;
    }
    int buffer_index = get_buffer_index(buffer);
    if(buffer_index == -1) return;
    current_context->bound_buffers[buffer_index] = name;
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    emesis_ensure_current_context();
    if (!current_context || n <= 0 || !buffers) return;
    es3_functions.glDeleteBuffers(n, buffers);
    for (GLsizei i = 0; i < n; i++) {
        GLuint b = buffers[i];
        if (b == 0) continue;
        for (int j = 0; j < MAX_BOUND_BUFFERS; j++) {
            if (current_context->bound_buffers[j] == b) {
                current_context->bound_buffers[j] = 0;
            }
        }
        invalidate_ubo_cache_for_buffer(b);
        if (current_context->default_vao_ebo == b) {
            current_context->default_vao_ebo = 0;
        }
        if (current_context->current_vao != 0 && emesis_get_current_ebo() == b) {
            unordered_map_remove(current_context->vao_ebo_map,
                                 (void*)(uintptr_t)current_context->current_vao);
        }
    }
}

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    emesis_ensure_current_context();
    if(!current_context) return;
    int generic_idx = get_buffer_index(target);
    if (target == GL_UNIFORM_BUFFER && index < MAX_UBO_SLOTS) {
        ubo_slot_binding_t* slot = &current_context->ubo_bindings[index];
        if (slot->valid && !slot->ranged && slot->buffer == buffer &&
            generic_idx >= 0 && current_context->bound_buffers[generic_idx] == buffer) {
            return;
        }
        slot->valid = true;
        slot->ranged = false;
        slot->buffer = buffer;
        slot->offset = 0;
        slot->size = 0;
    }
    if (generic_idx >= 0) {
        current_context->bound_buffers[generic_idx] = buffer;
    }
    es3_functions.glBindBufferBase(target, index, buffer);
}

void glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    emesis_ensure_current_context();
    if(!current_context) return;
    int generic_idx = get_buffer_index(target);
    if (target == GL_UNIFORM_BUFFER && index < MAX_UBO_SLOTS) {
        ubo_slot_binding_t* slot = &current_context->ubo_bindings[index];
        if (slot->valid && slot->ranged && slot->buffer == buffer &&
            slot->offset == offset && slot->size == size &&
            generic_idx >= 0 && current_context->bound_buffers[generic_idx] == buffer) {
            return;
        }
        slot->valid = true;
        slot->ranged = true;
        slot->buffer = buffer;
        slot->offset = offset;
        slot->size = size;
    }
    if (generic_idx >= 0) {
        current_context->bound_buffers[generic_idx] = buffer;
    }
    es3_functions.glBindBufferRange(target, index, buffer, offset, size);
}

void glUseProgram(GLuint program) {
    emesis_ensure_current_context();
    if(!current_context) return;
    if (current_context->program == program) return;
    es3_functions.glUseProgram(program);
    current_context->program = program;
}

void glGetIntegerv(GLenum pname, GLint* data) {
    emesis_ensure_current_context();
    if(!current_context || !data) return;
    switch (pname) {
        case GL_MAJOR_VERSION:
            *data = (GLint) gl_major;
            return;
        case GL_MINOR_VERSION:
            *data = (GLint) gl_minor;
            return;
        case GL_NUM_EXTENSIONS:
            es3_functions.glGetIntegerv(pname, data);
            (*data) += (GLint)current_context->nextras;
            return;
        case GL_MAX_COLOR_ATTACHMENTS:
            *data = MAX_FBTARGETS;
            return;
        case GL_MAX_DRAW_BUFFERS:
            *data = current_context->max_drawbuffers;
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

void glDepthRange(GLdouble nearVal, GLdouble farVal) {
    emesis_ensure_current_context();
    if(!current_context) return;
    es3_functions.glDepthRangef((GLfloat)nearVal, (GLfloat)farVal);
}

void glDeleteTextures(GLsizei n, const GLuint *textures) {
    emesis_ensure_current_context();
    if(!current_context || n <= 0 || !textures) return;
    es3_functions.glDeleteTextures(n, textures);
    for(GLsizei i = 0; i < n; i++) {
        GLuint tex = textures[i];
        if (tex == 0) continue;
        void* tracker = unordered_map_remove(current_context->texture_swztrack_map, (void*)(uintptr_t)tex);
        free(tracker);
        for (GLuint u = 0; u < MAX_TMUS; u++) {
            for (int t = 0; t < MAX_TEXTARGETS; t++) {
                if (current_context->bound_textures[u][t] == tex) {
                    current_context->bound_textures[u][t] = 0;
                }
            }
        }
    }
}

static bool buf_tex_trigger = false;

void glTexBuffer(GLenum target, GLenum internalFormat, GLuint buffer) {
    emesis_ensure_current_context();
    if(!current_context) return;
    if(current_context->es32 && es3_functions.glTexBuffer) {
        es3_functions.glTexBuffer(target, internalFormat, buffer);
    } else if(current_context->buffer_texture_ext && es3_functions.glTexBufferEXT) {
        es3_functions.glTexBufferEXT(target, internalFormat, buffer);
    } else if(current_context->buffer_texture_oes && es3_functions.glTexBufferOES) {
        es3_functions.glTexBufferOES(target, internalFormat, buffer);
    } else if(!buf_tex_trigger) {
        buf_tex_trigger = true;
        printf("EmesisRenderer: Buffer textures aren't supported on your device\n");
    }
}

void glTexBufferARB(GLenum target, GLenum internalFormat, GLuint buffer) {
    glTexBuffer(target, internalFormat, buffer);
}

void glTexBufferRange(GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    emesis_ensure_current_context();
    if(!current_context) return;
    if(current_context->es32 && es3_functions.glTexBufferRange) {
        es3_functions.glTexBufferRange(target, internalFormat, buffer, offset, size);
    } else if(current_context->buffer_texture_ext && es3_functions.glTexBufferRangeEXT) {
        es3_functions.glTexBufferRangeEXT(target, internalFormat, buffer, offset, size);
    } else if(current_context->buffer_texture_oes && es3_functions.glTexBufferRangeOES) {
        es3_functions.glTexBufferRangeOES(target, internalFormat, buffer, offset, size);
    } else if(!buf_tex_trigger) {
        buf_tex_trigger = true;
        printf("EmesisRenderer: Buffer textures aren't supported on your device\n");
    }
}

void glTexBufferRangeARB(GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    glTexBufferRange(target, internalFormat, buffer, offset, size);
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

static void emulate_clear_tex(GLuint texture, GLint level,
                              bool sub_region, GLint xoffset, GLint yoffset, GLint zoffset,
                              GLsizei width, GLsizei height, GLsizei depth,
                              GLenum format, GLenum type, const void* data) {
    if (!current_context || texture == 0) return;
    if (sub_region && (width <= 0 || height <= 0 || depth <= 0)) return;
    if (current_context->clear_fbo == 0) {
        es3_functions.glGenFramebuffers(1, &current_context->clear_fbo);
        if (current_context->clear_fbo == 0) return;
    }

    GLboolean scissor_was_on = es3_functions.glIsEnabled(GL_SCISSOR_TEST);
    GLint prev_scissor[4] = {0, 0, 0, 0};
    if (sub_region) {
        es3_functions.glGetIntegerv(GL_SCISSOR_BOX, prev_scissor);
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

    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_context->clear_fbo);
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
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_context->draw_framebuffer);

    if (attachment == GL_COLOR_ATTACHMENT0) {
        if (!prev_color_mask[0] || !prev_color_mask[1] || !prev_color_mask[2] || !prev_color_mask[3]) {
            es3_functions.glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
        }
    } else if ((attachment == GL_DEPTH_ATTACHMENT || attachment == GL_DEPTH_STENCIL_ATTACHMENT) && !prev_depth_mask) {
        es3_functions.glDepthMask(GL_FALSE);
    }

    if (sub_region) {
        es3_functions.glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
        if (!scissor_was_on) es3_functions.glDisable(GL_SCISSOR_TEST);
    } else if (scissor_was_on) {
        es3_functions.glEnable(GL_SCISSOR_TEST);
    }
}

void glClearTexImage(GLuint texture, GLint level, GLenum format, GLenum type, const void * data) {
    emesis_ensure_current_context();
    if(!current_context || texture == 0) return;
    if(current_context->clear_texture && es3_functions.glClearTexImageEXT) {
        es3_functions.glClearTexImageEXT(texture, level, format, type, data);
        return;
    }
    emulate_clear_tex(texture, level, false, 0, 0, 0, 0, 0, 1, format, type, data);
}

void glClearTexImageARB(GLuint texture, GLint level, GLenum format, GLenum type, const void * data) {
    glClearTexImage(texture, level, format, type, data);
}

void glClearTexSubImage(GLuint texture, GLint level,
                        GLint xoffset, GLint yoffset, GLint zoffset,
                        GLsizei width, GLsizei height, GLsizei depth,
                        GLenum format, GLenum type,
                        const void * data) {
    emesis_ensure_current_context();
    if(!current_context || texture == 0) return;
    if(current_context->clear_texture && es3_functions.glClearTexSubImageEXT) {
        es3_functions.glClearTexSubImageEXT(texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, data);
        return;
    }
    emulate_clear_tex(texture, level, true, xoffset, yoffset, zoffset, width, height, depth, format, type, data);
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
    bool version_overriden = extract_gl_version(getenv("MESA_GL_VERSION_OVERRIDE"));
    if(version_overriden) printf("EmesisRenderer will override exposed OpenGL version to %u.%u\n", gl_major, gl_minor);
    if(coherent_dynamic_storage) printf("EmesisRenderer will force dynamic storage buffers to be coherent.\n");
    if(debug) printf("EmesisRenderer will allow GL_DEBUG_OUTPUT to be enabled.\n");
}

GLenum glGetError(void) {
    if(noerror) return 0;
    else return es3_functions.glGetError();
}

void glDebugMessageControl(GLenum source, GLenum type, GLenum severity,
                           GLsizei count, const GLuint *ids, GLboolean enabled) {
    (void)source; (void)type; (void)severity; (void)count; (void)ids; (void)enabled;
}
