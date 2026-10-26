/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include "proc.h"
#include "egl.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "libraryinternal.h"

#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif
#ifndef GL_ABGR_EXT
#define GL_ABGR_EXT 0x8000
#endif

static void swizzle_process_bgra(GLenum* swizzle) {
    GLenum red_src = swizzle[0];
    GLenum blue_src = swizzle[2];
    swizzle[0] = blue_src;
    swizzle[2] = red_src;
}

static void swizzle_process_endianness(GLenum* swizzle) {
    GLenum orig_swizzle[4];
    memcpy(orig_swizzle, swizzle, 4 * sizeof(GLenum));
    swizzle[0] = orig_swizzle[3];
    swizzle[1] = orig_swizzle[2];
    swizzle[2] = orig_swizzle[1];
    swizzle[3] = orig_swizzle[0];
}

static texture_swizzle_track_t* get_or_create_swizzle_track(GLuint texture) {
    if (texture == 0 || !current_context) return NULL;
    texture_swizzle_track_t* track = unordered_map_get(current_context->texture_swztrack_map, (void*)(uintptr_t)texture);
    if(track == NULL) {
        track = calloc(1, sizeof(texture_swizzle_track_t));
        if (!track) return NULL;
        track->original_swizzle[0] = GL_RED;
        track->original_swizzle[1] = GL_GREEN;
        track->original_swizzle[2] = GL_BLUE;
        track->original_swizzle[3] = GL_ALPHA;
        unordered_map_put(current_context->texture_swztrack_map, (void*)(uintptr_t)texture, track);
    }
    return track;
}

static void apply_swizzles(GLenum target, texture_swizzle_track_t* track) {
    GLenum new_swizzle[4];
    memcpy(new_swizzle, track->original_swizzle, 4 * sizeof(GLenum));
    if(track->goofy_byte_order) swizzle_process_endianness(new_swizzle);
    if(track->upload_bgra) swizzle_process_bgra(new_swizzle);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, (GLint)new_swizzle[0]);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, (GLint)new_swizzle[1]);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, (GLint)new_swizzle[2]);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, (GLint)new_swizzle[3]);
}

INTERNAL void swizzle_process_upload(GLenum target, GLenum* format, GLenum* type) {
    if (!current_context) return;
    bool apply_upload_bgra = false;
    bool apply_goofy_order = false;
    if((*format) == GL_BGRA_EXT) {
        apply_upload_bgra = true;
        *format = GL_RGBA;
    } else if ((*format) == GL_BGR) {
        apply_upload_bgra = true;
        *format = GL_RGB;
    } else if ((*format) == GL_ABGR_EXT) {
        apply_goofy_order = true;
        *format = GL_RGBA;
    }
    if((*type) == 0x8035) { /* GL_UNSIGNED_INT_8_8_8_8 */
        apply_goofy_order = !apply_goofy_order;
        *type = GL_UNSIGNED_BYTE;
    } else if((*type) == 0x8367) { /* GL_UNSIGNED_INT_8_8_8_8_REV */
        *type = GL_UNSIGNED_BYTE;
    }
    GLuint texture = emesis_get_bound_texture(target);
    if (texture == 0) return;
    if (!apply_upload_bgra && !apply_goofy_order) {
        texture_swizzle_track_t* existing = unordered_map_get(
            current_context->texture_swztrack_map, (void*)(uintptr_t)texture);
        if (existing == NULL) return;
        if (!existing->goofy_byte_order && !existing->upload_bgra) return;
        existing->goofy_byte_order = false;
        existing->upload_bgra = false;
        apply_swizzles(target, existing);
        return;
    }
    texture_swizzle_track_t* track = get_or_create_swizzle_track(texture);
    if(track == NULL) return;
    if(apply_goofy_order != (bool)track->goofy_byte_order || apply_upload_bgra != (bool)track->upload_bgra) {
        track->goofy_byte_order = apply_goofy_order;
        track->upload_bgra = apply_upload_bgra;
        apply_swizzles(target, track);
    }
}

INTERNAL void swizzle_process_swizzle_param(GLenum target, GLenum swizzle_param, const GLenum* swizzle) {
    switch (swizzle_param) {
        case GL_TEXTURE_SWIZZLE_R:
        case GL_TEXTURE_SWIZZLE_G:
        case GL_TEXTURE_SWIZZLE_B:
        case GL_TEXTURE_SWIZZLE_A:
        case GL_TEXTURE_SWIZZLE_RGBA:
            break;
        default:
            return;
    }
    GLuint texture = emesis_get_bound_texture(target);
    texture_swizzle_track_t* track = get_or_create_swizzle_track(texture);
    if(track == NULL) return;
    switch(swizzle_param) {
        case GL_TEXTURE_SWIZZLE_R:
        case GL_TEXTURE_SWIZZLE_G:
        case GL_TEXTURE_SWIZZLE_B:
        case GL_TEXTURE_SWIZZLE_A:
            track->original_swizzle[swizzle_param - GL_TEXTURE_SWIZZLE_R] = *swizzle;
            apply_swizzles(target, track);
            break;
        case GL_TEXTURE_SWIZZLE_RGBA:
            memcpy(track->original_swizzle, swizzle, 4 * sizeof(GLenum));
            apply_swizzles(target, track);
            break;
    }
}