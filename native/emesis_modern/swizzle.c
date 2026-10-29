/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include <stdlib.h>
#include <string.h>
#include "GL/gl.h"
#include "swizzle.h"
#include "egl.h"

static void apply_swizzle(const GLenum* swizzle) {
    es3_functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, (GLint)swizzle[0]);
    es3_functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, (GLint)swizzle[1]);
    es3_functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, (GLint)swizzle[2]);
    es3_functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, (GLint)swizzle[3]);
}

static GLenum swizzle_get_B2L(GLenum component) {
    switch (component) {
        case GL_RED: return GL_ALPHA;
        case GL_GREEN: return GL_BLUE;
        case GL_BLUE: return GL_GREEN;
        case GL_ALPHA: return GL_RED;
        default: return component;
    }
}

static GLenum swizzle_B2G(GLenum component) {
    switch (component) {
        case GL_BLUE: return GL_RED;
        case GL_RED: return GL_BLUE;
        default: return component;
    }
}

static void swizzle_convert_B2L(GLenum* swizzle) {
    swizzle[0] = swizzle_get_B2L(swizzle[0]);
    swizzle[1] = swizzle_get_B2L(swizzle[1]);
    swizzle[2] = swizzle_get_B2L(swizzle[2]);
    swizzle[3] = swizzle_get_B2L(swizzle[3]);
}

static void swizzle_convert_BGR(GLenum* swizzle) {
    swizzle[0] = swizzle_B2G(swizzle[0]);
    swizzle[1] = swizzle_B2G(swizzle[1]);
    swizzle[2] = swizzle_B2G(swizzle[2]);
    swizzle[3] = swizzle_B2G(swizzle[3]);
}

static texture_swizzle_track_t* get_or_create_track(context_t* ctx, GLuint bound_texture) {
    texture_swizzle_track_t* track = unordered_map_get(ctx->texture_swztrack_map, (void*)(uintptr_t)bound_texture);
    if (track == NULL) {
        track = calloc(1, sizeof(texture_swizzle_track_t));
        if (!track) return NULL;
        track->original_swizzle[0] = GL_RED;
        track->original_swizzle[1] = GL_GREEN;
        track->original_swizzle[2] = GL_BLUE;
        track->original_swizzle[3] = GL_ALPHA;
        unordered_map_put(ctx->texture_swztrack_map, (void*)(uintptr_t)bound_texture, track);
        if (bound_texture < EMESIS_FAST_TEX_MAX) {
            ctx->tex_flags_fast[bound_texture] |= EMESIS_TEX_FLAG_SWIZZLE;
        }
    }
    return track;
}

static void apply_tracked_swizzle(const texture_swizzle_track_t* track) {
    GLenum swizzle[4];
    memcpy(swizzle, track->original_swizzle, sizeof(swizzle));
    if (track->goofy_byte_order) swizzle_convert_B2L(swizzle);
    if (track->upload_bgra) swizzle_convert_BGR(swizzle);
    apply_swizzle(swizzle);
}

void swizzle_process_upload(GLenum target, GLenum *format, GLenum *type) {
    if (*type == GL_UNSIGNED_INT_8_8_8_8_REV) {
        *type = GL_UNSIGNED_BYTE;
    }
    if (target != GL_TEXTURE_2D) return;
    context_t* ctx = current_context;
    if (!ctx) return;

    GLuint bound_texture = emesis_get_bound_texture(target);
    if (bound_texture == 0) return;

    bool needs_special = (*format == GL_BGRA_EXT || *type == GL_UNSIGNED_INT_8_8_8_8);
    if (__builtin_expect(!needs_special, 1)) {
        if (bound_texture < EMESIS_FAST_TEX_MAX &&
            (ctx->tex_flags_fast[bound_texture] & EMESIS_TEX_FLAG_SWIZZLE) == 0) {
            /* Zero-hash-lookup fast path for standard RGBA/RGB/Depth uploads (99.9% of calls) */
            return;
        }
        texture_swizzle_track_t* existing = unordered_map_get(ctx->texture_swztrack_map, (void*)(uintptr_t)bound_texture);
        if (!existing) return;
        bool changed = false;
        if (existing->upload_bgra) { existing->upload_bgra = false; changed = true; }
        if (existing->goofy_byte_order) { existing->goofy_byte_order = false; changed = true; }
        if (changed) apply_tracked_swizzle(existing);
        return;
    }

    texture_swizzle_track_t* track = get_or_create_track(ctx, bound_texture);
    if (!track) return;

    GLboolean new_bgra = (*format == GL_BGRA_EXT) ? GL_TRUE : GL_FALSE;
    if (new_bgra) *format = GL_RGBA;

    GLboolean new_goofy = (*type == GL_UNSIGNED_INT_8_8_8_8) ? GL_TRUE : GL_FALSE;
    if (new_goofy) *type = GL_UNSIGNED_BYTE;

    if (track->upload_bgra != new_bgra || track->goofy_byte_order != new_goofy) {
        track->upload_bgra = new_bgra;
        track->goofy_byte_order = new_goofy;
        apply_tracked_swizzle(track);
    }
}

static int swizzle_idx(GLenum swizzle_param) {
    switch (swizzle_param) {
        case GL_TEXTURE_SWIZZLE_R: return 0;
        case GL_TEXTURE_SWIZZLE_G: return 1;
        case GL_TEXTURE_SWIZZLE_B: return 2;
        case GL_TEXTURE_SWIZZLE_A: return 3;
        case GL_TEXTURE_SWIZZLE_RGBA: return 4;
        default: return -1;
    }
}

void swizzle_process_swizzle_param(GLenum target, GLenum swizzle_param, const GLenum* swizzle) {
    if (target != GL_TEXTURE_2D) return;
    context_t* ctx = current_context;
    if (!ctx) return;
    int idx = swizzle_idx(swizzle_param);
    if (idx == -1) return;
    GLuint bound_texture = emesis_get_bound_texture(target);
    if (bound_texture == 0) return;

    texture_swizzle_track_t* track = get_or_create_track(ctx, bound_texture);
    if (!track) return;

    if (idx == 4) {
        memcpy(track->original_swizzle, swizzle, sizeof(GLenum) * 4);
    } else {
        track->original_swizzle[idx] = *swizzle;
    }
    apply_tracked_swizzle(track);
}
