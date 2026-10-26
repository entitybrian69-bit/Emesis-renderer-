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

static void buffer_copier_store(GLint x, GLint y, GLsizei w, GLsizei h) {
    framebuffer_copier_t* copier = &current_context->framebuffer_copier;
    if(!copier->ready) return;
    GLuint current_texbind = emesis_get_bound_texture(GL_TEXTURE_2D);
    es3_functions.glBindTexture(GL_TEXTURE_2D, copier->temp_texture);
    es3_functions.glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    es3_functions.glBindTexture(GL_TEXTURE_2D, current_texbind);
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copier->tempfb);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, copier->temp_texture, 0);
    es3_functions.glBlitFramebuffer(x, y, x+w, y+h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_context->draw_framebuffer);
}

static void buffer_copier_release(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h) {
    framebuffer_copier_t* copier = &current_context->framebuffer_copier;
    if(!copier->ready) return;
    GLuint current_texbind = emesis_get_bound_texture(target);
    if(current_texbind == 0) return;
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copier->destfb);
    es3_functions.glBindFramebuffer(GL_READ_FRAMEBUFFER, copier->tempfb);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, current_texbind, level);
    es3_functions.glBlitFramebuffer(0, 0, w, h, x, y, x+w, y+h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, 0, 0);
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_context->draw_framebuffer);
    es3_functions.glBindFramebuffer(GL_READ_FRAMEBUFFER, current_context->read_framebuffer);
}

void glGetTexImage(GLenum target,
                   GLint level,
                   GLenum format,
                   GLenum type,
                   void * pixels) {
    emesis_ensure_current_context();
    if(!current_context || !pixels) return;
    if(!current_context->es31 || !es3_functions.glGetTexLevelParameteriv) {
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
    framebuffer_copier_t* copier = &current_context->framebuffer_copier;
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
    es3_functions.glBindFramebuffer(GL_READ_FRAMEBUFFER, current_context->read_framebuffer);
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
    emesis_ensure_current_context();
    if(!current_context) return;
    if(format == GL_DEPTH_COMPONENT) {
        framebuffer_copier_t* copier = &current_context->framebuffer_copier;
        copier->depthData = data;
        copier->depthWidth = width;
        copier->depthHeight = height;
        buffer_copier_store(x, y, width, height);
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

void glTexSubImage2D(GLenum target,
                     GLint level,
                     GLint xoffset,
                     GLint yoffset,
                     GLsizei width,
                     GLsizei height,
                     GLenum format,
                     GLenum type,
                     const void * data) {
    emesis_ensure_current_context();
    if(!current_context) return;
    swizzle_process_upload(target, &format, &type);
    if(format == GL_DEPTH_COMPONENT) {
        framebuffer_copier_t* copier = &current_context->framebuffer_copier;
        if(width == copier->depthWidth && height == copier->depthHeight && copier->depthData == data) {
            buffer_copier_release(target, level, xoffset, yoffset, width, height);
            return;
        }
    }
    es3_functions.glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, data);
}

void texture_blit_framebuffer(GLenum target,
                              GLint level,
                              GLint xoffset,
                              GLint yoffset,
                              GLint x,
                              GLint y,
                              GLsizei width,
                              GLsizei height,
                              bool depth) {
    framebuffer_copier_t* copier = &current_context->framebuffer_copier;
    if(!copier->ready) return;

    GLenum fb_attachment;
    GLbitfield fb_blit_bit;
    if(depth) {
        fb_attachment = GL_DEPTH_ATTACHMENT;
        fb_blit_bit = GL_DEPTH_BUFFER_BIT;
    }else {
        fb_attachment = GL_COLOR_ATTACHMENT0;
        fb_blit_bit = GL_COLOR_BUFFER_BIT;
    }

    GLuint texture = emesis_get_bound_texture(target);
    if (texture == 0) return;
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copier->destfb);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, fb_attachment, target, texture, level);
    es3_functions.glBlitFramebuffer(x, y, width+x, height+y, xoffset, yoffset, width+xoffset, height+yoffset, fb_blit_bit, GL_NEAREST);
    es3_functions.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, fb_attachment, target, 0, 0);
    es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_context->draw_framebuffer);
}

void glCopyTexSubImage2D(GLenum target,
                         GLint level,
                         GLint xoffset,
                         GLint yoffset,
                         GLint x,
                         GLint y,
                         GLsizei width,
                         GLsizei height) {
    emesis_ensure_current_context();
    if (!current_context) return;
    if(current_context->es31 && es3_functions.glGetTexLevelParameteriv) {
        GLint depthtype = GL_NONE;
        es3_functions.glGetTexLevelParameteriv(target, level, GL_TEXTURE_DEPTH_TYPE, &depthtype);
        if(depthtype != GL_NONE) {
            texture_blit_framebuffer(target, level, xoffset, yoffset, x, y, width, height, true);
        }else {
            texture_blit_framebuffer(target, level, xoffset, yoffset, x, y, width, height, false);
        }
    } else {
        es3_functions.glGetError();
        es3_functions.glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
        if(es3_functions.glGetError() == GL_INVALID_OPERATION) {
            texture_blit_framebuffer(target, level, xoffset, yoffset, x, y, width, height, true);
        }
    }
}