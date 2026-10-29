/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include "proc.h"
#include "egl.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "swizzle.h"

void buffer_copier_init(context_t* context) {
    framebuffer_copier_t* copier = &context->framebuffer_copier;
    while(es3_functions.glGetError() != 0) {}
    es3_functions.glGenTextures(1, &copier->temp_texture);
    es3_functions.glGenFramebuffers(1, &copier->tempfb);
    es3_functions.glGenFramebuffers(1, &copier->destfb);
    es3_functions.glBindTexture(GL_TEXTURE_2D, copier->temp_texture);
    es3_functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    es3_functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    es3_functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    es3_functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    es3_functions.glBindTexture(GL_TEXTURE_2D, 0);
    GLenum error = es3_functions.glGetError();
    if(error != 0) {
        printf("EmesisRenderer: error while initializing buffer-copier: %x\n", error);
        return;
    }
    copier->ready = true;
}

static void buffer_copier_store(context_t* ctx, GLint x, GLint y, GLsizei w, GLsizei h) {
    framebuffer_copier_t* copier = &ctx->framebuffer_copier;
    if(!copier->ready) return;
    GLuint current_texbind = emesis_get_bound_texture(GL_TEXTURE_2D);
    es3_functions.glBindTexture(GL_TEXTURE_2D, copier->temp_texture);
    es3_functions.glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    es3_functions.glBindTexture(GL_TEXTURE_2D, current_texbind);
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copier->tempfb);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, copier->temp_texture, 0);
    es3_functions.glBlitFramebuffer(x, y, x+w, y+h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->draw_framebuffer);
}

static void buffer_copier_release(context_t* ctx, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h) {
    framebuffer_copier_t* copier = &ctx->framebuffer_copier;
    if(!copier->ready) return;
    GLuint current_texbind = emesis_get_bound_texture(target);
    if(current_texbind == 0) return;
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copier->destfb);
    es3_functions.glBindFramebuffer(GL_READ_FRAMEBUFFER, copier->tempfb);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, current_texbind, level);
    es3_functions.glBlitFramebuffer(0, 0, w, h, x, y, x+w, y+h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, 0, 0);
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->draw_framebuffer);
    es3_functions.glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx->read_framebuffer);
}

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void * pixels) {
    context_t* ctx = emesis_get_context();
    if(!ctx || !pixels) return;
    if(!ctx->es31 || !es3_functions.glGetTexLevelParameteriv) {
        printf("EmesisRenderer: glGetTexImage only supported on OpenGL ES 3.1+\n");
        return;
    }
    bool swap_bgra = false;
    GLenum read_fmt = format;
    GLenum read_type = type;
    if (read_fmt == GL_BGRA_EXT) {
        read_fmt = GL_RGBA;
        swap_bgra = true;
    }
    if (read_type == 0x8367 /* GL_UNSIGNED_INT_8_8_8_8_REV */) {
        read_type = GL_UNSIGNED_BYTE;
    }
    if ((read_fmt != GL_RGBA && read_fmt != GL_RGBA_INTEGER) ||
        (read_type != GL_UNSIGNED_BYTE && read_type != GL_UNSIGNED_INT && read_type != GL_INT && read_type != GL_FLOAT)) {
        printf("EmesisRenderer: unsupported parameters for glGetTexImage (fmt=0x%x type=0x%x)\n", format, type);
        return;
    }
    framebuffer_copier_t* copier = &ctx->framebuffer_copier;
    GLuint texture = emesis_get_bound_texture(target);
    if (texture == 0) return;
    GLint w = 0, h = 0;
    es3_functions.glGetTexLevelParameteriv(target, level, GL_TEXTURE_WIDTH, &w);
    es3_functions.glGetTexLevelParameteriv(target, level, GL_TEXTURE_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;
    es3_functions.glBindFramebuffer(GL_READ_FRAMEBUFFER, copier->tempfb);
    es3_functions.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, texture, level);
    es3_functions.glReadPixels(0, 0, w, h, read_fmt, read_type, pixels);
    es3_functions.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, 0, 0);
    es3_functions.glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx->read_framebuffer);
    if (swap_bgra && read_type == GL_UNSIGNED_BYTE) {
        uint8_t* p = (uint8_t*)pixels;
        size_t total = (size_t)w * (size_t)h;
        for (size_t i = 0; i < total; i++) {
            uint8_t r = p[i * 4 + 0];
            p[i * 4 + 0] = p[i * 4 + 2];
            p[i * 4 + 2] = r;
        }
    }
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid * data) {
    context_t* ctx = emesis_get_context();
    if(!ctx) return;
    if(format == GL_DEPTH_COMPONENT) {
        framebuffer_copier_t* copier = &ctx->framebuffer_copier;
        copier->depthData = data;
        copier->depthWidth = width;
        copier->depthHeight = height;
        buffer_copier_store(ctx, x, y, width, height);
        return;
    }
    if (type == 0x8367 /* GL_UNSIGNED_INT_8_8_8_8_REV */) {
        type = GL_UNSIGNED_BYTE;
    }
    if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE && data != NULL && width > 0 && height > 0) {
        es3_functions.glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
        uint8_t* p = (uint8_t*)data;
        size_t total = (size_t)width * (size_t)height;
        for (size_t i = 0; i < total; i++) {
            uint8_t r = p[i * 4 + 0];
            p[i * 4 + 0] = p[i * 4 + 2];
            p[i * 4 + 2] = r;
        }
        return;
    }
    es3_functions.glReadPixels(x, y, width, height, format, type, data);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type, const void * data) {
    context_t* ctx = emesis_get_context();
    if(!ctx) return;
    if (target == 0x8064 /* GL_PROXY_TEXTURE_2D */) return;
    if (target == 0x0DE0 /* GL_TEXTURE_1D */) target = GL_TEXTURE_2D;
    swizzle_process_upload(target, &format, &type);
    if(format == GL_DEPTH_COMPONENT) {
        framebuffer_copier_t* copier = &ctx->framebuffer_copier;
        if(width == copier->depthWidth && height == copier->depthHeight && copier->depthData == data) {
            buffer_copier_release(ctx, target, level, xoffset, yoffset, width, height);
            return;
        }
    }
    es3_functions.glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, data);
}

static void texture_blit_framebuffer(context_t* ctx, GLenum target, GLint level,
                                     GLint xoffset, GLint yoffset,
                                     GLint x, GLint y, GLsizei width, GLsizei height, bool depth) {
    framebuffer_copier_t* copier = &ctx->framebuffer_copier;
    if(!copier->ready) return;

    GLenum fb_attachment = depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
    GLbitfield fb_blit_bit = depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT;

    GLuint texture = emesis_get_bound_texture(target);
    if (texture == 0) return;
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copier->destfb);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, fb_attachment, target, texture, level);
    es3_functions.glBlitFramebuffer(x, y, width+x, height+y, xoffset, yoffset, width+xoffset, height+yoffset, fb_blit_bit, GL_NEAREST);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, fb_attachment, target, 0, 0);
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->draw_framebuffer);
}

static bool is_depth_format_enum(GLenum fmt) {
    switch (fmt) {
        case GL_DEPTH_COMPONENT:
        case GL_DEPTH_STENCIL:
        case GL_DEPTH_COMPONENT16:
        case GL_DEPTH_COMPONENT24:
        case GL_DEPTH_COMPONENT32F:
        case GL_DEPTH24_STENCIL8:
        case GL_DEPTH32F_STENCIL8:
        case 0x81A7: /* GL_DEPTH_COMPONENT32 */
            return true;
        default:
            return false;
    }
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;

    GLuint bound_tex = emesis_get_bound_texture(target);
    if (bound_tex > 0 && bound_tex < EMESIS_FAST_TEX_MAX && ctx->tex_intfmt_fast[bound_tex] != 0) {
        /* O(1) zero-query fast path: no glGetTexLevelParameteriv and no glGetError() pipeline stalls! */
        if ((ctx->tex_flags_fast[bound_tex] & EMESIS_TEX_FLAG_DEPTH) != 0) {
            texture_blit_framebuffer(ctx, target, level, xoffset, yoffset, x, y, width, height, true);
        } else {
            es3_functions.glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
        }
        return;
    }

    if(ctx->es31 && es3_functions.glGetTexLevelParameteriv) {
        GLint internalFormat = 0;
        es3_functions.glGetTexLevelParameteriv(target, level, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
        bool is_depth = is_depth_format_enum((GLenum)internalFormat);
        if (bound_tex > 0 && bound_tex < EMESIS_FAST_TEX_MAX && internalFormat != 0) {
            ctx->tex_intfmt_fast[bound_tex] = (GLenum)internalFormat;
            if (is_depth) ctx->tex_flags_fast[bound_tex] |= EMESIS_TEX_FLAG_DEPTH;
        }
        if (is_depth) {
            texture_blit_framebuffer(ctx, target, level, xoffset, yoffset, x, y, width, height, true);
        } else {
            es3_functions.glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
        }
    } else {
        es3_functions.glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
    }
}
