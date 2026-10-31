/**
 * Emesis Modern Direct State Access (GL_ARB_direct_state_access / GL 4.5)
 *
 * Accelerates Minecraft 26.3 RenderPearl DirectStateAccess.Core, Sodium 26.3
 * MappedStagingBuffer chunk uploads (collapsing 5 JNI+driver calls per chunk
 * section down to 1 cached GPU DMA copy with zero buffer unbinds), and Iris
 * DSAARB shaderpack passes.
 */

#include <GL/gl.h>
#include <GL/glext.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "proc.h"
#include "egl.h"
#include "main.h"

extern void glBufferStorage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
extern void *glMapBuffer(GLenum target, GLenum access);
extern void glBindVertexBuffers(GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLsizei *strides);
extern void glVertexAttribLFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset);

/* ============================================================================
 * 1. DSA Buffer Objects (Zero-Unbind Cached GL_COPY_READ/WRITE_BUFFER Path)
 * ============================================================================ */

void glCreateBuffers(GLsizei n, GLuint *buffers) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !buffers) return;
    es3_functions.glGenBuffers(n, buffers);
    for (GLsizei i = 0; i < n; i++) {
        if (buffers[i] != 0) {
            glBindBuffer(GL_COPY_WRITE_BUFFER, buffers[i]);
        }
    }
}

void glNamedBufferStorage(GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags) {
    if (buffer == 0) return;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    glBufferStorage(GL_COPY_WRITE_BUFFER, size, data, flags);
}

void glNamedBufferData(GLuint buffer, GLsizeiptr size, const void *data, GLenum usage) {
    if (buffer == 0) return;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    glBufferData(GL_COPY_WRITE_BUFFER, size, data, usage);
}

void glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data) {
    if (buffer == 0 || size <= 0 || !data) return;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    glBufferSubData(GL_COPY_WRITE_BUFFER, offset, size, data);
}

void glCopyNamedBufferSubData(GLuint readBuffer, GLuint writeBuffer,
                              GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
    if (readBuffer == 0 || writeBuffer == 0 || size <= 0) return;
    /*
     * Zero-unbind fast path: glBindBuffer deduplicates against ctx->bound_buffers[],
     * so across 50-300+ Sodium chunk section uploads in a frame, readBuffer (Sodium's
     * MappedStagingBuffer) is bound ONCE and writeBuffer is bound ONCE per RenderRegion!
     */
    glBindBuffer(GL_COPY_READ_BUFFER, readBuffer);
    glBindBuffer(GL_COPY_WRITE_BUFFER, writeBuffer);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, readOffset, writeOffset, size);
}

void glClearNamedBufferSubData(GLuint buffer, GLenum internalformat,
                               GLintptr offset, GLsizeiptr size,
                               GLenum format, GLenum type, const void *data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || buffer == 0 || size <= 0) return;
    (void)internalformat; (void)format; (void)type;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    void* dst = es3_functions.glMapBufferRange(GL_COPY_WRITE_BUFFER, offset, size,
                                               GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
    if (dst) {
        if (!data) {
            memset(dst, 0, (size_t)size);
        } else {
            /* Fast zero-check (most common clear value) */
            const uint8_t* bytes = (const uint8_t*)data;
            if (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0) {
                memset(dst, 0, (size_t)size);
            } else {
                uint32_t pat = 0;
                memcpy(&pat, data, 4);
                uint32_t* p32 = (uint32_t*)dst;
                size_t words = (size_t)size / 4u;
                for (size_t i = 0; i < words; i++) p32[i] = pat;
                size_t rem = (size_t)size & 3u;
                if (rem) memcpy((uint8_t*)dst + words * 4u, &pat, rem);
            }
        }
        es3_functions.glUnmapBuffer(GL_COPY_WRITE_BUFFER);
    }
}

void glClearNamedBufferData(GLuint buffer, GLenum internalformat,
                            GLenum format, GLenum type, const void *data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || buffer == 0) return;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    GLint64 sz = 0;
    if (es3_functions.glGetBufferParameteri64v) {
        es3_functions.glGetBufferParameteri64v(GL_COPY_WRITE_BUFFER, GL_BUFFER_SIZE, &sz);
    } else {
        GLint sz32 = 0;
        es3_functions.glGetBufferParameteriv(GL_COPY_WRITE_BUFFER, GL_BUFFER_SIZE, &sz32);
        sz = sz32;
    }
    if (sz > 0) {
        glClearNamedBufferSubData(buffer, internalformat, 0, (GLsizeiptr)sz, format, type, data);
    }
}

void *glMapNamedBuffer(GLuint buffer, GLenum access) {
    if (buffer == 0) return NULL;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    return glMapBuffer(GL_COPY_WRITE_BUFFER, access);
}

void *glMapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    if (buffer == 0) return NULL;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    return glMapBufferRange(GL_COPY_WRITE_BUFFER, offset, length, access);
}

GLboolean glUnmapNamedBuffer(GLuint buffer) {
    if (buffer == 0) return GL_FALSE;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    return glUnmapBuffer(GL_COPY_WRITE_BUFFER);
}

void glFlushMappedNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length) {
    if (buffer == 0) return;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    glFlushMappedBufferRange(GL_COPY_WRITE_BUFFER, offset, length);
}

void glGetNamedBufferParameteriv(GLuint buffer, GLenum pname, GLint *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || buffer == 0 || !params) return;
    glBindBuffer(GL_COPY_READ_BUFFER, buffer);
    es3_functions.glGetBufferParameteriv(GL_COPY_READ_BUFFER, pname, params);
}

void glGetNamedBufferParameteri64v(GLuint buffer, GLenum pname, GLint64 *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || buffer == 0 || !params) return;
    glBindBuffer(GL_COPY_READ_BUFFER, buffer);
    if (es3_functions.glGetBufferParameteri64v) {
        es3_functions.glGetBufferParameteri64v(GL_COPY_READ_BUFFER, pname, params);
    } else {
        GLint v32 = 0;
        es3_functions.glGetBufferParameteriv(GL_COPY_READ_BUFFER, pname, &v32);
        *params = (GLint64)v32;
    }
}

void glGetNamedBufferPointerv(GLuint buffer, GLenum pname, void **params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || buffer == 0 || !params) return;
    glBindBuffer(GL_COPY_READ_BUFFER, buffer);
    es3_functions.glGetBufferPointerv(GL_COPY_READ_BUFFER, pname, params);
}

void glGetNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, void *data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || buffer == 0 || size <= 0 || !data) return;
    glBindBuffer(GL_COPY_READ_BUFFER, buffer);
    const void* src = es3_functions.glMapBufferRange(GL_COPY_READ_BUFFER, offset, size, GL_MAP_READ_BIT);
    if (src) {
        memcpy(data, src, (size_t)size);
        es3_functions.glUnmapBuffer(GL_COPY_READ_BUFFER);
    }
}

/* ============================================================================
 * 2. DSA Framebuffer & Renderbuffer Objects
 * ============================================================================ */

void glCreateFramebuffers(GLsizei n, GLuint *framebuffers) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !framebuffers) return;
    glGenFramebuffers(n, framebuffers);
    GLuint prev_draw = ctx->draw_framebuffer;
    bool changed = false;
    for (GLsizei i = 0; i < n; i++) {
        if (framebuffers[i] != 0) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[i]);
            changed = true;
        }
    }
    if (changed) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glNamedFramebufferRenderbuffer(GLuint framebuffer, GLenum attachment,
                                    GLenum renderbuffertarget, GLuint renderbuffer) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachment, renderbuffertarget, renderbuffer);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glNamedFramebufferParameteri(GLuint framebuffer, GLenum pname, GLint param) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !ctx->es31 || !es3_functions.glFramebufferParameteri) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    es3_functions.glFramebufferParameteri(GL_DRAW_FRAMEBUFFER, pname, param);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glNamedFramebufferTexture(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    if (texture == 0) {
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
    } else {
        GLenum ttarget = emesis_resolve_tex_target(ctx, texture);
        if (ttarget == GL_TEXTURE_2D) {
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture, level);
        } else if (ttarget == GL_TEXTURE_2D_ARRAY || ttarget == GL_TEXTURE_3D || ttarget == GL_TEXTURE_CUBE_MAP_ARRAY) {
            glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, attachment, texture, level, 0);
        } else {
            glFramebufferTexture(GL_DRAW_FRAMEBUFFER, attachment, texture, level);
        }
    }
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glNamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment,
                                    GLuint texture, GLint level, GLint layer) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, attachment, texture, level, layer);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glNamedFramebufferDrawBuffer(GLuint framebuffer, GLenum buf) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDrawBuffer(buf);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glNamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum *bufs) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDrawBuffers(n, bufs);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glNamedFramebufferReadBuffer(GLuint framebuffer, GLenum src) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    GLuint prev_read = ctx->read_framebuffer;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glReadBuffer(src);
    if (prev_read != framebuffer) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read);
    }
}

void glInvalidateNamedFramebufferData(GLuint framebuffer, GLsizei numAttachments, const GLenum *attachments) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !es3_functions.glInvalidateFramebuffer) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    es3_functions.glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, numAttachments, attachments);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glInvalidateNamedFramebufferSubData(GLuint framebuffer, GLsizei numAttachments,
                                         const GLenum *attachments, GLint x, GLint y,
                                         GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !es3_functions.glInvalidateSubFramebuffer) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    es3_functions.glInvalidateSubFramebuffer(GL_DRAW_FRAMEBUFFER, numAttachments, attachments, x, y, width, height);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glClearNamedFramebufferiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint *value) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !value) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glClearBufferiv(buffer, drawbuffer, value);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glClearNamedFramebufferuiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLuint *value) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !value) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glClearBufferuiv(buffer, drawbuffer, value);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat *value) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !value) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glClearBufferfv(buffer, drawbuffer, value);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glClearNamedFramebufferfi(GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    es3_functions.glClearBufferfi(buffer, drawbuffer, depth, stencil);
    if (prev_draw != framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

void glBlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer,
                            GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                            GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                            GLbitfield mask, GLenum filter) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    GLuint prev_read = ctx->read_framebuffer;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
    es3_functions.glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
    if (prev_read != readFramebuffer) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read);
    }
    if (prev_draw != drawFramebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    }
}

GLenum glCheckNamedFramebufferStatus(GLuint framebuffer, GLenum target) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return GL_FRAMEBUFFER_COMPLETE;
    if (target == GL_READ_FRAMEBUFFER) {
        GLuint prev_read = ctx->read_framebuffer;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        GLenum st = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        if (prev_read != framebuffer) glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read);
        return st;
    }
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    GLenum st = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (prev_draw != framebuffer) glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    return st;
}

void glGetNamedFramebufferParameteriv(GLuint framebuffer, GLenum pname, GLint *param) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !param || !ctx->es31 || !es3_functions.glGetFramebufferParameteriv) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    es3_functions.glGetFramebufferParameteriv(GL_DRAW_FRAMEBUFFER, pname, param);
    if (prev_draw != framebuffer) glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
}

void glGetNamedFramebufferAttachmentParameteriv(GLuint framebuffer, GLenum attachment,
                                                GLenum pname, GLint *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    GLuint prev_draw = ctx->draw_framebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, pname, params);
    if (prev_draw != framebuffer) glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
}

void glCreateRenderbuffers(GLsizei n, GLuint *renderbuffers) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !renderbuffers) return;
    es3_functions.glGenRenderbuffers(n, renderbuffers);
    GLint prev_rb = 0;
    es3_functions.glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rb);
    for (GLsizei i = 0; i < n; i++) {
        if (renderbuffers[i] != 0) {
            es3_functions.glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[i]);
        }
    }
    es3_functions.glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev_rb);
}

void glNamedRenderbufferStorage(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx || renderbuffer == 0) return;
    GLint prev_rb = 0;
    es3_functions.glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rb);
    es3_functions.glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, internalformat, width, height);
    if ((GLuint)prev_rb != renderbuffer) {
        es3_functions.glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev_rb);
    }
}

void glNamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples,
                                           GLenum internalformat, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx || renderbuffer == 0) return;
    GLint prev_rb = 0;
    es3_functions.glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rb);
    es3_functions.glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    es3_functions.glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, internalformat, width, height);
    if ((GLuint)prev_rb != renderbuffer) {
        es3_functions.glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev_rb);
    }
}

void glGetNamedRenderbufferParameteriv(GLuint renderbuffer, GLenum pname, GLint *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || renderbuffer == 0 || !params) return;
    GLint prev_rb = 0;
    es3_functions.glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rb);
    es3_functions.glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    es3_functions.glGetRenderbufferParameteriv(GL_RENDERBUFFER, pname, params);
    if ((GLuint)prev_rb != renderbuffer) {
        es3_functions.glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev_rb);
    }
}

/* ============================================================================
 * 3. DSA Texture & Sampler Objects
 * ============================================================================ */

static inline GLenum dsa_bind_tex_temp(context_t* ctx, GLuint texture, GLuint* out_prev_tex) {
    GLenum target = emesis_resolve_tex_target(ctx, texture);
    GLuint prev = emesis_get_bound_texture(target);
    *out_prev_tex = prev;
    if (prev != texture) {
        glBindTexture(target, texture);
    }
    return target;
}

static inline void dsa_restore_tex_temp(GLenum target, GLuint texture, GLuint prev_tex) {
    if (prev_tex != texture) {
        glBindTexture(target, prev_tex);
    }
}

void glCreateTextures(GLenum target, GLsizei n, GLuint *textures) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !textures) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    es3_functions.glGenTextures(n, textures);
    int t_idx = emesis_tex_target_index(target);
    GLuint prev_tex = emesis_get_bound_texture(target);
    bool bound_any = false;
    for (GLsizei i = 0; i < n; i++) {
        GLuint tex = textures[i];
        if (tex == 0) continue;
        if (tex < EMESIS_FAST_TEX_MAX && t_idx >= 0) {
            ctx->tex_target_fast[tex] = (uint8_t)(t_idx + 1);
        }
        glBindTexture(target, tex);
        bound_any = true;
    }
    if (bound_any) {
        glBindTexture(target, prev_tex);
    }
}

void glBindTextureUnit(GLuint unit, GLuint texture) {
    context_t* ctx = emesis_get_context();
    if (!ctx || unit >= MAX_TMUS) return;

    if (texture == 0) {
        GLuint orig_unit = ctx->active_tex_unit_valid ? ctx->active_tex_unit : 0;
        bool switched = false;
        for (int t = 0; t < MAX_TEXTARGETS; t++) {
            uint16_t bit = (uint16_t)(1u << t);
            if ((ctx->bound_textures_valid[unit] & bit) != 0 && ctx->bound_textures[unit][t] == 0) {
                continue;
            }
            if ((ctx->bound_textures_valid[unit] & bit) == 0 && t != 0) {
                continue;
            }
            if (!switched && (!ctx->active_tex_unit_valid || orig_unit != unit)) {
                es3_functions.glActiveTexture(GL_TEXTURE0 + unit);
                switched = true;
            }
            es3_functions.glBindTexture(emesis_tex_index_to_target(t), 0);
            ctx->bound_textures[unit][t] = 0;
            ctx->bound_textures_valid[unit] |= bit;
        }
        if (switched) {
            es3_functions.glActiveTexture(GL_TEXTURE0 + orig_unit);
            ctx->active_tex_unit = orig_unit;
            ctx->active_tex_unit_valid = true;
        }
        return;
    }

    GLenum target = emesis_resolve_tex_target(ctx, texture);
    int t_idx = emesis_tex_target_index(target);
    if (t_idx >= 0) {
        uint16_t bit = (uint16_t)(1u << t_idx);
        if ((ctx->bound_textures_valid[unit] & bit) != 0 && ctx->bound_textures[unit][t_idx] == texture) {
            return;
        }
        GLuint orig_unit = ctx->active_tex_unit_valid ? ctx->active_tex_unit : 0;
        bool need_switch = (!ctx->active_tex_unit_valid || orig_unit != unit);
        if (need_switch) {
            es3_functions.glActiveTexture(GL_TEXTURE0 + unit);
        }
        es3_functions.glBindTexture(target, texture);
        ctx->bound_textures[unit][t_idx] = texture;
        ctx->bound_textures_valid[unit] |= bit;
        if (need_switch) {
            es3_functions.glActiveTexture(GL_TEXTURE0 + orig_unit);
            ctx->active_tex_unit = orig_unit;
            ctx->active_tex_unit_valid = true;
        }
    }
}

void glTextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    if (texture < EMESIS_FAST_TEX_MAX) {
        ctx->tex_target_fast[texture] = (uint8_t)(emesis_tex_target_index(GL_TEXTURE_BUFFER) + 1);
    }
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexBuffer(GL_TEXTURE_BUFFER, internalformat, buffer);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureBufferRange(GLuint texture, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    if (texture < EMESIS_FAST_TEX_MAX) {
        ctx->tex_target_fast[texture] = (uint8_t)(emesis_tex_target_index(GL_TEXTURE_BUFFER) + 1);
    }
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexBufferRange(GL_TEXTURE_BUFFER, internalformat, buffer, offset, size);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureStorage1D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexStorage2D(target, levels, internalformat, width, 1);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureStorage2D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexStorage2D(target, levels, internalformat, width, height);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureStorage3D(GLuint texture, GLsizei levels, GLenum internalformat,
                        GLsizei width, GLsizei height, GLsizei depth) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexStorage3D(target, levels, internalformat, width, height, depth);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureStorage2DMultisample(GLuint texture, GLsizei samples, GLenum internalformat,
                                   GLsizei width, GLsizei height, GLboolean fixedsamplelocations) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !ctx->es31 || !es3_functions.glTexStorage2DMultisample) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    es3_functions.glTexStorage2DMultisample(target, samples, internalformat, width, height, fixedsamplelocations);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureStorage3DMultisample(GLuint texture, GLsizei samples, GLenum internalformat,
                                   GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !ctx->es32 || !es3_functions.glTexStorage3DMultisample) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    es3_functions.glTexStorage3DMultisample(target, samples, internalformat, width, height, depth, fixedsamplelocations);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLsizei width,
                         GLenum format, GLenum type, const void *pixels) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexSubImage2D(target, level, xoffset, 0, width, 1, format, type, pixels);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset,
                         GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset,
                         GLsizei width, GLsizei height, GLsizei depth,
                         GLenum format, GLenum type, const void *pixels) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    if (target == GL_TEXTURE_CUBE_MAP && depth == 1 && zoffset >= 0 && zoffset < 6) {
        glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)zoffset, level, xoffset, yoffset, width, height, format, type, pixels);
    } else {
        es3_functions.glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
    }
    dsa_restore_tex_temp(target, texture, prev);
}

void glCompressedTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLsizei width,
                                   GLenum format, GLsizei imageSize, const void *data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    es3_functions.glCompressedTexSubImage2D(target, level, xoffset, 0, width, 1, format, imageSize, data);
    dsa_restore_tex_temp(target, texture, prev);
}

void glCompressedTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset,
                                   GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    es3_functions.glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
    dsa_restore_tex_temp(target, texture, prev);
}

void glCompressedTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset,
                                   GLsizei width, GLsizei height, GLsizei depth,
                                   GLenum format, GLsizei imageSize, const void *data) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    es3_functions.glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
    dsa_restore_tex_temp(target, texture, prev);
}

void glCopyTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glCopyTexSubImage2D(target, level, xoffset, 0, x, y, width, 1);
    dsa_restore_tex_temp(target, texture, prev);
}

void glCopyTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset,
                             GLint x, GLint y, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
    dsa_restore_tex_temp(target, texture, prev);
}

void glCopyTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset,
                             GLint x, GLint y, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    if (target == GL_TEXTURE_CUBE_MAP && zoffset >= 0 && zoffset < 6) {
        glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)zoffset, level, xoffset, yoffset, x, y, width, height);
    } else {
        es3_functions.glCopyTexSubImage3D(target, level, xoffset, yoffset, zoffset, x, y, width, height);
    }
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureParameterf(GLuint texture, GLenum pname, GLfloat param) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexParameterf(target, pname, param);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureParameterfv(GLuint texture, GLenum pname, const GLfloat *param) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !param) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexParameterfv(target, pname, param);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureParameteri(GLuint texture, GLenum pname, GLint param) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexParameteri(target, pname, param);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureParameteriv(GLuint texture, GLenum pname, const GLint *param) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !param) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexParameteriv(target, pname, param);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureParameterIiv(GLuint texture, GLenum pname, const GLint *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !params) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexParameterIiv(target, pname, params);
    dsa_restore_tex_temp(target, texture, prev);
}

void glTextureParameterIuiv(GLuint texture, GLenum pname, const GLuint *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !params) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glTexParameterIuiv(target, pname, params);
    dsa_restore_tex_temp(target, texture, prev);
}

void glGenerateTextureMipmap(GLuint texture) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    es3_functions.glGenerateMipmap(target);
    dsa_restore_tex_temp(target, texture, prev);
}

void glGetTextureParameterfv(GLuint texture, GLenum pname, GLfloat *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !params) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    es3_functions.glGetTexParameterfv(target, pname, params);
    dsa_restore_tex_temp(target, texture, prev);
}

void glGetTextureParameteriv(GLuint texture, GLenum pname, GLint *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !params) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    es3_functions.glGetTexParameteriv(target, pname, params);
    dsa_restore_tex_temp(target, texture, prev);
}

void glGetTextureParameterIiv(GLuint texture, GLenum pname, GLint *params) {
    glGetTextureParameteriv(texture, pname, params);
}

void glGetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint *params) {
    glGetTextureParameteriv(texture, pname, (GLint*)params);
}

void glGetTextureLevelParameterfv(GLuint texture, GLint level, GLenum pname, GLfloat *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !params) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    GLenum qtarget = (target == GL_TEXTURE_CUBE_MAP) ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : target;
    glGetTexLevelParameterfv(qtarget, level, pname, params);
    dsa_restore_tex_temp(target, texture, prev);
}

void glGetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname, GLint *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !params) return;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    GLenum qtarget = (target == GL_TEXTURE_CUBE_MAP) ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : target;
    glGetTexLevelParameteriv(qtarget, level, pname, params);
    dsa_restore_tex_temp(target, texture, prev);
}

void glGetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void *pixels) {
    context_t* ctx = emesis_get_context();
    if (!ctx || texture == 0 || !pixels || bufSize <= 0) return;
    (void)bufSize;
    GLuint prev = 0;
    GLenum target = dsa_bind_tex_temp(ctx, texture, &prev);
    glGetTexImage(target, level, format, type, pixels);
    dsa_restore_tex_temp(target, texture, prev);
}

/* ============================================================================
 * 4. DSA Vertex Array, Sampler, Pipeline & Query Objects
 * ============================================================================ */

void glCreateVertexArrays(GLsizei n, GLuint *arrays) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !arrays) return;
    es3_functions.glGenVertexArrays(n, arrays);
    GLuint prev_vao = ctx->current_vao;
    bool changed = false;
    for (GLsizei i = 0; i < n; i++) {
        if (arrays[i] != 0) {
            glBindVertexArray(arrays[i]);
            changed = true;
        }
    }
    if (changed) {
        glBindVertexArray(prev_vao);
    }
}

void glDisableVertexArrayAttrib(GLuint vaobj, GLuint index) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    es3_functions.glDisableVertexAttribArray(index);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glEnableVertexArrayAttrib(GLuint vaobj, GLuint index) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    es3_functions.glEnableVertexAttribArray(index);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glVertexArrayElementBuffer(GLuint vaobj, GLuint buffer) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glVertexArrayVertexBuffer(GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    glBindVertexBuffer(bindingindex, buffer, offset, stride);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glVertexArrayVertexBuffers(GLuint vaobj, GLuint first, GLsizei count,
                                const GLuint *buffers, const GLintptr *offsets, const GLsizei *strides) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    glBindVertexBuffers(first, count, buffers, offsets, strides);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glVertexArrayAttribBinding(GLuint vaobj, GLuint attribindex, GLuint bindingindex) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    glVertexAttribBinding(attribindex, bindingindex);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glVertexArrayAttribFormat(GLuint vaobj, GLuint attribindex, GLint size,
                               GLenum type, GLboolean normalized, GLuint relativeoffset) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    glVertexAttribFormat(attribindex, size, type, normalized, relativeoffset);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glVertexArrayAttribIFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    glVertexAttribIFormat(attribindex, size, type, relativeoffset);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glVertexArrayAttribLFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    glVertexAttribLFormat(attribindex, size, type, relativeoffset);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glVertexArrayBindingDivisor(GLuint vaobj, GLuint bindingindex, GLuint divisor) {
    context_t* ctx = emesis_get_context();
    if (!ctx || vaobj == 0) return;
    GLuint prev_vao = ctx->current_vao;
    glBindVertexArray(vaobj);
    glVertexBindingDivisor(bindingindex, divisor);
    if (prev_vao != vaobj) glBindVertexArray(prev_vao);
}

void glCreateSamplers(GLsizei n, GLuint *samplers) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !samplers) return;
    es3_functions.glGenSamplers(n, samplers);
}

void glCreateProgramPipelines(GLsizei n, GLuint *pipelines) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !pipelines || !ctx->es31 || !es3_functions.glGenProgramPipelines) return;
    es3_functions.glGenProgramPipelines(n, pipelines);
}

void glCreateQueries(GLenum target, GLsizei n, GLuint *ids) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n <= 0 || !ids) return;
    (void)target;
    es3_functions.glGenQueries(n, ids);
}
