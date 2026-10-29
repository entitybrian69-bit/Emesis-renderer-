/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include "proc.h"
#include "egl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef GL_FRONT_LEFT
#define GL_FRONT_LEFT 0x0400
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_TEXTURE_1D
#define GL_TEXTURE_1D 0x0DE0
#endif
#ifndef GL_FRAMEBUFFER_DEFAULT
#define GL_FRAMEBUFFER_DEFAULT 0x8218
#endif

#define EMESIS_FB_GEN_TEXTURE 0xFFFFFFFFu

static framebuffer_t* alloc_default_fbo(void) {
    framebuffer_t* fb = calloc(1, sizeof(framebuffer_t));
    if (!fb) return NULL;
    fb->nbuffers = 1;
    fb->virt_drawbuffers[0] = GL_COLOR_ATTACHMENT0;
    fb->phys_drawbuffers[0] = GL_COLOR_ATTACHMENT0;
    fb->sent_es_nbuffers = 1;
    fb->sent_es_drawbuffers[0] = GL_COLOR_ATTACHMENT0;
    fb->attachments_dirty = false;
    return fb;
}

static framebuffer_t* get_or_create_framebuffer(context_t* ctx, GLuint fb_id) {
    if (!ctx || fb_id == 0) return NULL;
    framebuffer_t* fb = unordered_map_get(ctx->framebuffer_map, (void*)(uintptr_t)fb_id);
    if (!fb) {
        fb = alloc_default_fbo();
        if (fb) {
            unordered_map_put(ctx->framebuffer_map, (void*)(uintptr_t)fb_id, fb);
        }
    }
    return fb;
}

static framebuffer_t* get_framebuffer(context_t* ctx, GLenum target) {
    if (!ctx) return NULL;
    GLuint fb = 0;
    switch (target) {
        case GL_FRAMEBUFFER:
        case GL_DRAW_FRAMEBUFFER: fb = ctx->draw_framebuffer; break;
        case GL_READ_FRAMEBUFFER: fb = ctx->read_framebuffer; break;
        default: return NULL;
    }
    if (fb == 0) return NULL;
    return get_or_create_framebuffer(ctx, fb);
}

static GLuint get_attachment_idx(context_t* ctx, GLenum attachment) {
    if (attachment < GL_COLOR_ATTACHMENT0) return (GLuint)-1;
    GLuint idx = attachment - GL_COLOR_ATTACHMENT0;
    GLuint max_att = (GLuint)ctx->max_drawbuffers;
    if (max_att == 0 || max_att > MAX_FBTARGETS) max_att = MAX_FBTARGETS;
    if (idx >= max_att) return (GLuint)-1;
    return idx;
}

static void apply_phys_slot(context_t* ctx, GLenum target, framebuffer_t* fb, GLuint phys_idx,
                            GLuint desired_target, GLuint desired_obj, GLint desired_level, GLint desired_layer) {
    if (fb->phys_targets[phys_idx] == desired_target &&
        fb->phys_objects[phys_idx] == desired_obj &&
        fb->phys_levels[phys_idx] == desired_level &&
        fb->phys_layers[phys_idx] == desired_layer) {
        return;
    }
    GLenum phys_attachment = GL_COLOR_ATTACHMENT0 + phys_idx;
    switch (desired_target) {
        case GL_NONE:
            if (fb->phys_targets[phys_idx] == GL_RENDERBUFFER) {
                es3_functions.glFramebufferRenderbuffer(target, phys_attachment, GL_RENDERBUFFER, 0);
            } else {
                es3_functions.glFramebufferTexture2D(target, phys_attachment, GL_TEXTURE_2D, 0, 0);
            }
            break;
        case GL_RENDERBUFFER:
            es3_functions.glFramebufferRenderbuffer(target, phys_attachment, GL_RENDERBUFFER, desired_obj);
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
            es3_functions.glFramebufferTextureLayer(target, phys_attachment, desired_obj, desired_level, desired_layer);
            break;
        case EMESIS_FB_GEN_TEXTURE:
            if (ctx->framebuffertexture) {
                ctx->framebuffertexture(target, phys_attachment, desired_obj, desired_level);
            } else {
                GLenum ttarget = GL_TEXTURE_2D;
                if (desired_obj > 0 && desired_obj < EMESIS_FAST_TEX_MAX && ctx->tex_target_fast[desired_obj] != 0) {
                    ttarget = emesis_tex_index_to_target((int)ctx->tex_target_fast[desired_obj] - 1);
                }
                if (ttarget == GL_TEXTURE_2D_ARRAY || ttarget == GL_TEXTURE_3D || ttarget == GL_TEXTURE_CUBE_MAP_ARRAY) {
                    es3_functions.glFramebufferTextureLayer(target, phys_attachment, desired_obj, desired_level, 0);
                } else {
                    if (ttarget == GL_TEXTURE_1D || ttarget == GL_NONE) ttarget = GL_TEXTURE_2D;
                    es3_functions.glFramebufferTexture2D(target, phys_attachment, ttarget, desired_obj, desired_level);
                }
            }
            break;
        default:
            es3_functions.glFramebufferTexture2D(target, phys_attachment, desired_target, desired_obj, desired_level);
            break;
    }
    fb->phys_targets[phys_idx] = desired_target;
    fb->phys_objects[phys_idx] = desired_obj;
    fb->phys_levels[phys_idx] = desired_level;
    fb->phys_layers[phys_idx] = desired_layer;
}

static bool is_es_native_drawbuffers(GLsizei n, const GLenum* bufs) {
    for (GLsizei i = 0; i < n; i++) {
        GLenum b = bufs[i];
        if (b != GL_NONE && b != (GLenum)(GL_COLOR_ATTACHMENT0 + i)) {
            return false;
        }
    }
    return true;
}

static void sync_fbo_drawbuffers(context_t* ctx, GLenum fbtarget, framebuffer_t* fb) {
    GLsizei n = fb->nbuffers;
    GLuint max_att = (GLuint)ctx->max_drawbuffers;
    if (max_att == 0 || max_att > MAX_FBTARGETS) max_att = MAX_FBTARGETS;

    if (is_es_native_drawbuffers(n, fb->virt_drawbuffers)) {
        /* Identity/native ES3 layout: every logical slot k lives at physical slot k! */
        for (GLuint k = 0; k < max_att; k++) {
            apply_phys_slot(ctx, fbtarget, fb, k,
                            fb->color_targets[k], fb->color_objects[k],
                            fb->color_levels[k], fb->color_layers[k]);
        }
        for (GLsizei i = 0; i < n; i++) {
            fb->phys_drawbuffers[i] = fb->virt_drawbuffers[i];
        }
    } else {
        /* Non-identity desktop MRT remap (e.g. {GL_COLOR_ATTACHMENT4} at slot 0):
         * Map active virtual drawbuffers i -> physical slot i; leave unreferenced slots untouched
         * unless they hold an alias that must be moved, avoiding unnecessary attachment churn. */
        bool slot_claimed[MAX_FBTARGETS] = {false};
        for (GLsizei i = 0; i < n; i++) {
            GLenum vb = fb->virt_drawbuffers[i];
            if (vb == GL_NONE) {
                fb->phys_drawbuffers[i] = GL_NONE;
                continue;
            }
            GLuint v_idx = get_attachment_idx(ctx, vb);
            if (v_idx == (GLuint)-1) {
                fb->phys_drawbuffers[i] = GL_NONE;
                continue;
            }
            slot_claimed[i] = true;
            apply_phys_slot(ctx, fbtarget, fb, (GLuint)i,
                            fb->color_targets[v_idx], fb->color_objects[v_idx],
                            fb->color_levels[v_idx], fb->color_layers[v_idx]);
            fb->phys_drawbuffers[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
        }
        (void)slot_claimed;
    }

    fb->attachments_dirty = false;

    if (fb->sent_es_nbuffers != n ||
        (n > 0 && memcmp(fb->sent_es_drawbuffers, fb->phys_drawbuffers, (size_t)n * sizeof(GLenum)) != 0)) {
        es3_functions.glDrawBuffers(n, fb->phys_drawbuffers);
        fb->sent_es_nbuffers = n;
        if (n > 0) {
            memcpy(fb->sent_es_drawbuffers, fb->phys_drawbuffers, (size_t)n * sizeof(GLenum));
        }
    }
}

static void on_logical_color_update(context_t* ctx, GLenum target, framebuffer_t* fb, GLuint att_idx) {
    /* Fast path (99.9%): if drawbuffers layout is native ES3 identity, update ONLY slot att_idx! */
    if (is_es_native_drawbuffers(fb->nbuffers, fb->virt_drawbuffers)) {
        apply_phys_slot(ctx, target, fb, att_idx,
                        fb->color_targets[att_idx], fb->color_objects[att_idx],
                        fb->color_levels[att_idx], fb->color_layers[att_idx]);
        return;
    }
    /* Non-identity drawbuffers active: update any physical slot i currently referencing GL_COLOR_ATTACHMENT0 + att_idx */
    GLenum virt_att = GL_COLOR_ATTACHMENT0 + att_idx;
    for (GLsizei i = 0; i < fb->nbuffers; i++) {
        if (fb->virt_drawbuffers[i] == virt_att) {
            apply_phys_slot(ctx, target, fb, (GLuint)i,
                            fb->color_targets[att_idx], fb->color_objects[att_idx],
                            fb->color_levels[att_idx], fb->color_layers[att_idx]);
        }
    }
}

void emesis_fbo_on_delete_textures(context_t* ctx, GLsizei n, const GLuint* textures) {
    if (!ctx || !ctx->framebuffer_map || n <= 0 || !textures) return;
    for (unordered_map_entry* e = ctx->framebuffer_map->head; e != NULL; e = e->next) {
        framebuffer_t* fb = (framebuffer_t*)e->value;
        if (!fb) continue;
        for (GLsizei t = 0; t < n; t++) {
            GLuint tex = textures[t];
            if (tex == 0) continue;
            for (int k = 0; k < MAX_FBTARGETS; k++) {
                if (fb->color_targets[k] != GL_NONE && fb->color_targets[k] != GL_RENDERBUFFER && fb->color_objects[k] == tex) {
                    fb->color_targets[k] = GL_NONE;
                    fb->color_objects[k] = 0;
                    fb->color_levels[k] = 0;
                    fb->color_layers[k] = 0;
                }
                if (fb->phys_targets[k] != GL_NONE && fb->phys_targets[k] != GL_RENDERBUFFER && fb->phys_objects[k] == tex) {
                    fb->phys_targets[k] = GL_NONE;
                    fb->phys_objects[k] = 0;
                    fb->phys_levels[k] = 0;
                    fb->phys_layers[k] = 0;
                    fb->attachments_dirty = true;
                }
            }
        }
    }
}

void glClearBufferiv(GLenum buffer, GLint drawBuffer, const GLint * value) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (buffer == GL_COLOR) {
        framebuffer_t *fb = get_framebuffer(ctx, GL_DRAW_FRAMEBUFFER);
        if (fb) {
            if (drawBuffer < 0 || drawBuffer >= fb->nbuffers || fb->virt_drawbuffers[drawBuffer] == GL_NONE) return;
        }
    }
    es3_functions.glClearBufferiv(buffer, drawBuffer, value);
}

void glClearBufferuiv(GLenum buffer, GLint drawBuffer, const GLuint * value) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (buffer == GL_COLOR) {
        framebuffer_t *fb = get_framebuffer(ctx, GL_DRAW_FRAMEBUFFER);
        if (fb) {
            if (drawBuffer < 0 || drawBuffer >= fb->nbuffers || fb->virt_drawbuffers[drawBuffer] == GL_NONE) return;
        }
    }
    es3_functions.glClearBufferuiv(buffer, drawBuffer, value);
}

void glClearBufferfv(GLenum buffer, GLint drawBuffer, const GLfloat * value) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (buffer == GL_COLOR) {
        framebuffer_t *fb = get_framebuffer(ctx, GL_DRAW_FRAMEBUFFER);
        if (fb) {
            if (drawBuffer < 0 || drawBuffer >= fb->nbuffers || fb->virt_drawbuffers[drawBuffer] == GL_NONE) return;
        }
    }
    es3_functions.glClearBufferfv(buffer, drawBuffer, value);
}

static GLenum normalize_default_fbo_buf(GLenum buf) {
    if (buf == GL_NONE || buf == GL_BACK) return buf;
    if (buf == GL_FRONT || buf == GL_FRONT_LEFT || buf == GL_BACK_LEFT || buf == GL_FRONT_AND_BACK) {
        return GL_BACK;
    }
    return buf;
}

void glDrawBuffers(GLsizei n, const GLenum* buffers) {
    context_t* ctx = emesis_get_context();
    if (!ctx || n < 0) return;
    framebuffer_t *fb = get_framebuffer(ctx, GL_DRAW_FRAMEBUFFER);
    if (!fb) {
        if (n == 1 && buffers) {
            GLenum b = normalize_default_fbo_buf(buffers[0]);
            es3_functions.glDrawBuffers(1, &b);
        } else {
            es3_functions.glDrawBuffers(n, buffers);
        }
        return;
    }
    if (n > MAX_DRAWBUFFERS) n = MAX_DRAWBUFFERS;

    if (!fb->attachments_dirty &&
        fb->nbuffers == n &&
        (n == 0 || (buffers && memcmp(fb->virt_drawbuffers, buffers, (size_t)n * sizeof(GLenum)) == 0))) {
        /* Exact same drawbuffers and attachments clean -> ZERO driver calls, TBDR pass preserved! */
        return;
    }

    fb->nbuffers = n;
    if (n > 0 && buffers) {
        memcpy(fb->virt_drawbuffers, buffers, (size_t)n * sizeof(GLenum));
    }
    sync_fbo_drawbuffers(ctx, GL_DRAW_FRAMEBUFFER, fb);
}

void glDrawBuffer(GLenum buffer) {
    glDrawBuffers(1, &buffer);
}

void glReadBuffer(GLenum mode) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    framebuffer_t* fb = get_framebuffer(ctx, GL_READ_FRAMEBUFFER);
    if (!fb) {
        es3_functions.glReadBuffer(normalize_default_fbo_buf(mode));
        return;
    }
    GLuint att_idx = get_attachment_idx(ctx, mode);
    if (att_idx != (GLuint)-1) {
        if (fb->phys_targets[att_idx] == fb->color_targets[att_idx] &&
            fb->phys_objects[att_idx] == fb->color_objects[att_idx] &&
            fb->phys_levels[att_idx] == fb->color_levels[att_idx] &&
            fb->phys_layers[att_idx] == fb->color_layers[att_idx]) {
            es3_functions.glReadBuffer(mode);
            return;
        }
        for (GLsizei i = 0; i < fb->nbuffers; i++) {
            if (fb->virt_drawbuffers[i] == mode) {
                es3_functions.glReadBuffer(GL_COLOR_ATTACHMENT0 + (GLenum)i);
                return;
            }
        }
        apply_phys_slot(ctx, GL_READ_FRAMEBUFFER, fb, att_idx,
                        fb->color_targets[att_idx], fb->color_objects[att_idx],
                        fb->color_levels[att_idx], fb->color_layers[att_idx]);
    }
    es3_functions.glReadBuffer(mode);
}

GLenum glCheckFramebufferStatus(GLenum target) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return GL_FRAMEBUFFER_UNDEFINED;
    GLenum framebuffer_status = es3_functions.glCheckFramebufferStatus(target);
    if (framebuffer_status == GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) {
        framebuffer_t *framebuffer = get_framebuffer(ctx, target);
        if (!framebuffer) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
        for (GLint i = 0; i < MAX_FBTARGETS; i++) {
            if (framebuffer->color_targets[i] != GL_NONE || framebuffer->color_objects[i] != 0)
                return GL_FRAMEBUFFER_COMPLETE;
        }
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    }
    return framebuffer_status;
}

void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    framebuffer_t *framebuffer = get_framebuffer(ctx, target);
    GLuint attachment_idx = get_attachment_idx(ctx, attachment);
    if (!framebuffer || attachment_idx == (GLuint)-1) {
        if (ctx->framebuffertexture) {
            ctx->framebuffertexture(target, attachment, texture, level);
        } else if (texture == 0) {
            es3_functions.glFramebufferTexture2D(target, attachment, GL_TEXTURE_2D, 0, 0);
        } else {
            GLenum ttarget = GL_TEXTURE_2D;
            if (texture < EMESIS_FAST_TEX_MAX && ctx->tex_target_fast[texture] != 0) {
                ttarget = emesis_tex_index_to_target((int)ctx->tex_target_fast[texture] - 1);
            }
            if (ttarget == GL_TEXTURE_2D_ARRAY || ttarget == GL_TEXTURE_3D || ttarget == GL_TEXTURE_CUBE_MAP_ARRAY) {
                es3_functions.glFramebufferTextureLayer(target, attachment, texture, level, 0);
            } else {
                if (ttarget == GL_TEXTURE_1D || ttarget == GL_NONE) ttarget = GL_TEXTURE_2D;
                es3_functions.glFramebufferTexture2D(target, attachment, ttarget, texture, level);
            }
        }
        return;
    }
    if (texture == 0) {
        framebuffer->color_targets[attachment_idx] = GL_NONE;
        framebuffer->color_objects[attachment_idx] = 0;
        framebuffer->color_levels[attachment_idx] = 0;
        framebuffer->color_layers[attachment_idx] = 0;
    } else {
        framebuffer->color_targets[attachment_idx] = EMESIS_FB_GEN_TEXTURE;
        framebuffer->color_objects[attachment_idx] = texture;
        framebuffer->color_levels[attachment_idx] = level;
        framebuffer->color_layers[attachment_idx] = 0;
    }
    on_logical_color_update(ctx, target, framebuffer, attachment_idx);
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    framebuffer_t *framebuffer = get_framebuffer(ctx, target);
    GLuint attachment_idx = get_attachment_idx(ctx, attachment);
    if (!framebuffer || attachment_idx == (GLuint)-1) {
        es3_functions.glFramebufferTexture2D(target, attachment, textarget, texture, level);
        return;
    }
    if (texture == 0) {
        framebuffer->color_targets[attachment_idx] = GL_NONE;
        framebuffer->color_objects[attachment_idx] = 0;
        framebuffer->color_levels[attachment_idx] = 0;
        framebuffer->color_layers[attachment_idx] = 0;
    } else {
        framebuffer->color_targets[attachment_idx] = textarget;
        framebuffer->color_objects[attachment_idx] = texture;
        framebuffer->color_levels[attachment_idx] = level;
        framebuffer->color_layers[attachment_idx] = 0;
    }
    on_logical_color_update(ctx, target, framebuffer, attachment_idx);
}

void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    framebuffer_t *framebuffer = get_framebuffer(ctx, target);
    GLuint attachment_idx = get_attachment_idx(ctx, attachment);
    if (!framebuffer || attachment_idx == (GLuint)-1) {
        es3_functions.glFramebufferTextureLayer(target, attachment, texture, level, layer);
        return;
    }
    if (texture == 0) {
        framebuffer->color_targets[attachment_idx] = GL_NONE;
        framebuffer->color_objects[attachment_idx] = 0;
        framebuffer->color_levels[attachment_idx] = 0;
        framebuffer->color_layers[attachment_idx] = 0;
    } else {
        framebuffer->color_targets[attachment_idx] = GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER;
        framebuffer->color_objects[attachment_idx] = texture;
        framebuffer->color_levels[attachment_idx] = level;
        framebuffer->color_layers[attachment_idx] = layer;
    }
    on_logical_color_update(ctx, target, framebuffer, attachment_idx);
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    framebuffer_t *framebuffer = get_framebuffer(ctx, target);
    GLuint attachment_idx = get_attachment_idx(ctx, attachment);
    if (!framebuffer || attachment_idx == (GLuint)-1) {
        es3_functions.glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
        return;
    }
    if (renderbuffer == 0) {
        framebuffer->color_targets[attachment_idx] = GL_NONE;
        framebuffer->color_objects[attachment_idx] = 0;
        framebuffer->color_levels[attachment_idx] = 0;
        framebuffer->color_layers[attachment_idx] = 0;
    } else {
        framebuffer->color_targets[attachment_idx] = renderbuffertarget;
        framebuffer->color_objects[attachment_idx] = renderbuffer;
        framebuffer->color_levels[attachment_idx] = 0;
        framebuffer->color_layers[attachment_idx] = 0;
    }
    on_logical_color_update(ctx, target, framebuffer, attachment_idx);
}

void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    framebuffer_t *framebuffer = get_framebuffer(ctx, target);
    GLuint attachment_idx = get_attachment_idx(ctx, attachment);
    if (!framebuffer || attachment_idx == (GLuint)-1) {
        es3_functions.glGetFramebufferAttachmentParameteriv(target, attachment, pname, params);
        return;
    }
    GLenum fb_target = framebuffer->color_targets[attachment_idx];
    switch (pname) {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            switch (fb_target) {
                case GL_NONE: *params = GL_NONE; break;
                case GL_RENDERBUFFER: *params = GL_RENDERBUFFER; break;
                default: *params = GL_TEXTURE; break;
            }
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
            *params = (fb_target == GL_NONE) ? 0 : (GLint)framebuffer->color_objects[attachment_idx];
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
            *params = (fb_target == GL_NONE || fb_target == GL_RENDERBUFFER) ? 0 : framebuffer->color_levels[attachment_idx];
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
            *params = (fb_target != GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER) ? 0 : framebuffer->color_layers[attachment_idx];
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
            if (fb_target < GL_TEXTURE_CUBE_MAP_POSITIVE_X || fb_target > GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
                *params = 0;
            } else {
                *params = (GLint)fb_target;
            }
            break;
        default: {
            GLenum phys_att = attachment;
            for (GLsizei i = 0; i < framebuffer->nbuffers; i++) {
                if (framebuffer->virt_drawbuffers[i] == attachment) {
                    phys_att = GL_COLOR_ATTACHMENT0 + (GLenum)i;
                    break;
                }
            }
            es3_functions.glGetFramebufferAttachmentParameteriv(target, phys_att, pname, params);
            break;
        }
    }
}

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    es3_functions.glGenFramebuffers(n, framebuffers);
    for (GLsizei i = 0; i < n; i++) {
        framebuffer_t* fb = alloc_default_fbo();
        if (fb) {
            unordered_map_put(ctx->framebuffer_map, (void*)(uintptr_t)framebuffers[i], fb);
        }
    }
}

void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    es3_functions.glDeleteFramebuffers(n, framebuffers);
    for (GLsizei i = 0; i < n; i++) {
        if (ctx->draw_framebuffer == framebuffers[i]) {
            ctx->draw_framebuffer = 0;
        }
        if (ctx->read_framebuffer == framebuffers[i]) {
            ctx->read_framebuffer = 0;
        }
        framebuffer_t* fb = unordered_map_remove(ctx->framebuffer_map, (void*)(uintptr_t)framebuffers[i]);
        if (fb) free(fb);
    }
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    context_t* ctx = emesis_get_context();
    if (!ctx) return;
    if (framebuffer != 0) {
        (void)get_or_create_framebuffer(ctx, framebuffer);
    }
    switch (target) {
        case GL_FRAMEBUFFER:
            if (ctx->draw_framebuffer_valid && ctx->read_framebuffer_valid &&
                ctx->draw_framebuffer == framebuffer && ctx->read_framebuffer == framebuffer) {
                return;
            }
            es3_functions.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            ctx->draw_framebuffer = framebuffer;
            ctx->read_framebuffer = framebuffer;
            ctx->draw_framebuffer_valid = true;
            ctx->read_framebuffer_valid = true;
            break;
        case GL_READ_FRAMEBUFFER:
            if (ctx->read_framebuffer_valid && ctx->read_framebuffer == framebuffer) {
                return;
            }
            es3_functions.glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
            ctx->read_framebuffer = framebuffer;
            ctx->read_framebuffer_valid = true;
            break;
        case GL_DRAW_FRAMEBUFFER:
            if (ctx->draw_framebuffer_valid && ctx->draw_framebuffer == framebuffer) {
                return;
            }
            es3_functions.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            ctx->draw_framebuffer = framebuffer;
            ctx->draw_framebuffer_valid = true;
            break;
        default:
            es3_functions.glBindFramebuffer(target, framebuffer);
            break;
    }
}
