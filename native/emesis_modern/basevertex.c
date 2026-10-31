/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "egl.h"
#include "proc.h"
#include "main.h"
#include "basevertex.h"

#ifndef GL_MAP_PERSISTENT_BIT_EXT
#define GL_MAP_PERSISTENT_BIT_EXT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT_EXT
#define GL_MAP_COHERENT_BIT_EXT 0x0080
#endif

#define EMESIS_INDIRECT_RING_SIZE_32 (1024u * 1024u)
#define EMESIS_INDIRECT_RING_SIZE_64 (2u * 1024u * 1024u)
#define EMESIS_ES30_IDX_RING_SIZE    (512u * 1024u)

void basevertex_init(context_t* context) {
    if (!context || !context->es31) return;

    int di_idx = get_buffer_index(GL_DRAW_INDIRECT_BUFFER);
    GLuint prev_di = (di_idx >= 0) ? context->bound_buffers[di_idx] : 0;

    size_t ring_size = (sizeof(void*) == 4) ? EMESIS_INDIRECT_RING_SIZE_32 : EMESIS_INDIRECT_RING_SIZE_64;
    es3_functions.glGenBuffers(1, &context->basevertex.indirectRenderBuffer);
    if (context->basevertex.indirectRenderBuffer == 0) return;

    es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, context->basevertex.indirectRenderBuffer);
    context->basevertex.indirectRingSize = ring_size;
    context->basevertex.indirectRingOffset = 0;
    context->basevertex.indirectMappedPtr = NULL;

    if (context->buffer_storage && es3_functions.glBufferStorageEXT) {
        GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT | GL_MAP_COHERENT_BIT_EXT;
        es3_functions.glBufferStorageEXT(GL_DRAW_INDIRECT_BUFFER, (GLsizeiptr)ring_size, NULL, flags);
        void* mapped = es3_functions.glMapBufferRange(GL_DRAW_INDIRECT_BUFFER, 0, (GLsizeiptr)ring_size, flags);
        if (mapped != NULL) {
            context->basevertex.indirectMappedPtr = mapped;
        }
    }
    if (context->basevertex.indirectMappedPtr == NULL) {
        es3_functions.glBufferData(GL_DRAW_INDIRECT_BUFFER, (GLsizeiptr)ring_size, NULL, GL_STREAM_DRAW);
    }

    es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, prev_di);
    context->basevertex.ready = true;
}

GLint type_bytes(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_SHORT: return 2;
        case GL_UNSIGNED_INT: return 4;
        case GL_UNSIGNED_BYTE: return 1;
        default: return -1;
    }
}

static bool ensure_indirect_scratch(basevertex_renderer_t* bv, size_t count) {
    if (count <= bv->scratchCap) return true;
    size_t new_cap = bv->scratchCap ? bv->scratchCap : 256;
    while (new_cap < count) new_cap *= 2;
    basevertex_indirect_cmd_t* p = realloc(bv->scratchPasses, new_cap * sizeof(basevertex_indirect_cmd_t));
    if (!p) return false;
    bv->scratchPasses = p;
    bv->scratchCap = new_cap;
    return true;
}

static bool ensure_es30_cpu_scratch(basevertex_renderer_t* bv, size_t index_count) {
    if (index_count <= bv->es30CpuCap) return true;
    size_t new_cap = bv->es30CpuCap ? bv->es30CpuCap : 4096;
    while (new_cap < index_count) new_cap *= 2;
    uint32_t* p = realloc(bv->es30CpuScratch, new_cap * sizeof(uint32_t));
    if (!p) return false;
    bv->es30CpuScratch = p;
    bv->es30CpuCap = new_cap;
    return true;
}

static bool upload_indirect_ring(context_t* ctx, const basevertex_indirect_cmd_t* cmds,
                                 GLsizei valid_draws, GLintptr* out_offset) {
    basevertex_renderer_t* bv = &ctx->basevertex;
    if (!bv->ready || bv->indirectRenderBuffer == 0 || valid_draws <= 0) return false;

    size_t byte_len = (size_t)valid_draws * sizeof(basevertex_indirect_cmd_t);
    size_t aligned_len = (byte_len + 15u) & ~15u;

    if (byte_len > bv->indirectRingSize) {
        if (bv->indirectMappedPtr != NULL) {
            /* Oversized batch > ring size: reallocate non-mapped streaming buffer safely */
            es3_functions.glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
            bv->indirectMappedPtr = NULL;
        }
        size_t new_ring = bv->indirectRingSize ? bv->indirectRingSize : EMESIS_INDIRECT_RING_SIZE_32;
        while (new_ring < aligned_len * 2u) new_ring *= 2u;
        es3_functions.glBufferData(GL_DRAW_INDIRECT_BUFFER, (GLsizeiptr)new_ring, NULL, GL_STREAM_DRAW);
        bv->indirectRingSize = new_ring;
        bv->indirectRingOffset = 0;
    }

    if (bv->indirectMappedPtr != NULL) {
        if (bv->indirectRingOffset + aligned_len > bv->indirectRingSize) {
            bv->indirectRingOffset = 0;
        }
        size_t off = bv->indirectRingOffset;
        memcpy((uint8_t*)bv->indirectMappedPtr + off, cmds, byte_len);
        bv->indirectRingOffset = off + aligned_len;
        *out_offset = (GLintptr)off;
        return true;
    }

    GLbitfield map_flags = GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
    if (bv->indirectRingOffset + aligned_len > bv->indirectRingSize) {
        bv->indirectRingOffset = 0;
        map_flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT;
    }
    size_t off = bv->indirectRingOffset;
    void* dst = es3_functions.glMapBufferRange(GL_DRAW_INDIRECT_BUFFER, (GLintptr)off, (GLsizeiptr)byte_len, map_flags);
    if (dst != NULL) {
        memcpy(dst, cmds, byte_len);
        es3_functions.glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
    } else {
        es3_functions.glBufferSubData(GL_DRAW_INDIRECT_BUFFER, (GLintptr)off, (GLsizeiptr)byte_len, cmds);
    }
    bv->indirectRingOffset = off + aligned_len;
    *out_offset = (GLintptr)off;
    return true;
}

bool emesis_dispatch_mdi_commands(context_t* ctx, GLenum mode, GLenum type,
                                  const basevertex_indirect_cmd_t* cmds, GLsizei valid_draws) {
    if (!ctx || !ctx->es31 || !ctx->basevertex.ready || valid_draws <= 0) return false;

    int di_idx = get_buffer_index(GL_DRAW_INDIRECT_BUFFER);
    GLuint prev_di = (di_idx >= 0) ? ctx->bound_buffers[di_idx] : 0;
    if (prev_di != ctx->basevertex.indirectRenderBuffer) {
        es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ctx->basevertex.indirectRenderBuffer);
    }

    GLintptr byte_offset = 0;
    if (!upload_indirect_ring(ctx, cmds, valid_draws, &byte_offset)) {
        if (prev_di != ctx->basevertex.indirectRenderBuffer) {
            es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, prev_di);
        }
        return false;
    }

    bool dispatched = false;
    if (valid_draws > 1 && ctx->multidraw_indirect && es3_functions.glMultiDrawElementsIndirectEXT) {
        es3_functions.glMultiDrawElementsIndirectEXT(mode, type, (const void*)byte_offset, valid_draws, 0);
        dispatched = true;
    }

    if (!dispatched && es3_functions.glDrawElementsIndirect) {
        for (GLsizei i = 0; i < valid_draws; i++) {
            GLintptr cmd_off = byte_offset + (GLintptr)((size_t)i * sizeof(basevertex_indirect_cmd_t));
            es3_functions.glDrawElementsIndirect(mode, type, (const void*)cmd_off);
        }
        dispatched = true;
    }

    if (prev_di != ctx->basevertex.indirectRenderBuffer) {
        es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, prev_di);
    }
    return dispatched;
}

static void rebase_indices_to_u32(uint32_t* restrict dst, const uint8_t* restrict src,
                                  GLsizei count, GLint tbytes, GLint basevtx) {
    if (tbytes == 4) {
        const uint32_t* s32 = (const uint32_t*)src;
        uint32_t bv = (uint32_t)basevtx;
        for (GLsizei i = 0; i < count; i++) {
            dst[i] = s32[i] + bv;
        }
    } else if (tbytes == 2) {
        const uint16_t* s16 = (const uint16_t*)src;
        for (GLsizei i = 0; i < count; i++) {
            int32_t v = (int32_t)s16[i] + (int32_t)basevtx;
            dst[i] = (v > 0) ? (uint32_t)v : 0u;
        }
    } else {
        for (GLsizei i = 0; i < count; i++) {
            int32_t v = (int32_t)src[i] + (int32_t)basevtx;
            dst[i] = (v > 0) ? (uint32_t)v : 0u;
        }
    }
}

static bool es30_upload_rebased_ring(context_t* ctx, const uint32_t* data, size_t total_indices, GLintptr* out_offset) {
    basevertex_renderer_t* bv = &ctx->basevertex;
    size_t byte_len = total_indices * sizeof(uint32_t);
    size_t aligned_len = (byte_len + 15u) & ~15u;

    if (bv->es30IndexBuffer == 0) {
        es3_functions.glGenBuffers(1, &bv->es30IndexBuffer);
        if (bv->es30IndexBuffer == 0) return false;
        bv->es30IndexRingSize = EMESIS_ES30_IDX_RING_SIZE;
        if (bv->es30IndexRingSize < aligned_len * 2u) bv->es30IndexRingSize = aligned_len * 2u;
        bv->es30IndexRingOffset = 0;
        es3_functions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bv->es30IndexBuffer);
        es3_functions.glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)bv->es30IndexRingSize, NULL, GL_STREAM_DRAW);
    } else {
        es3_functions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bv->es30IndexBuffer);
        if (byte_len > bv->es30IndexRingSize) {
            size_t new_ring = bv->es30IndexRingSize * 2u;
            while (new_ring < aligned_len * 2u) new_ring *= 2u;
            es3_functions.glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)new_ring, NULL, GL_STREAM_DRAW);
            bv->es30IndexRingSize = new_ring;
            bv->es30IndexRingOffset = 0;
        }
    }

    GLbitfield map_flags = GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
    if (bv->es30IndexRingOffset + aligned_len > bv->es30IndexRingSize) {
        bv->es30IndexRingOffset = 0;
        map_flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT;
    }
    size_t off = bv->es30IndexRingOffset;
    void* dst = es3_functions.glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)off, (GLsizeiptr)byte_len, map_flags);
    if (dst != NULL) {
        memcpy(dst, data, byte_len);
        es3_functions.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    } else {
        es3_functions.glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)off, (GLsizeiptr)byte_len, data);
    }
    bv->es30IndexRingOffset = off + aligned_len;
    *out_offset = (GLintptr)off;
    return true;
}

static void es30_draw_elements_basevertex(context_t* ctx, GLenum mode, GLsizei count, GLenum type,
                                          const void* indices, GLsizei instancecount,
                                          GLint basevertex, bool instanced) {
    if (count <= 0 || instancecount <= 0) return;
    if (basevertex == 0) {
        if (instanced) es3_functions.glDrawElementsInstanced(mode, count, type, indices, instancecount);
        else es3_functions.glDrawElements(mode, count, type, indices);
        return;
    }
    GLint tbytes = type_bytes(type);
    if (tbytes == -1) return;

    GLuint prev_ebo = emesis_get_current_ebo();
    const uint8_t* src_bytes = NULL;

    if (prev_ebo != 0) {
        size_t byte_off = (size_t)(uintptr_t)indices;
        size_t needed_end = byte_off + (size_t)count * (size_t)tbytes;
        const uint8_t* mirror = emesis_es30_get_ebo_cpu_ptr(ctx, prev_ebo, needed_end);
        if (!mirror) return;
        src_bytes = mirror + byte_off;
    } else {
        if (!indices) return;
        src_bytes = (const uint8_t*)indices;
    }

    if (!ensure_es30_cpu_scratch(&ctx->basevertex, (size_t)count)) return;
    rebase_indices_to_u32(ctx->basevertex.es30CpuScratch, src_bytes, count, tbytes, basevertex);

    GLintptr ring_off = 0;
    if (es30_upload_rebased_ring(ctx, ctx->basevertex.es30CpuScratch, (size_t)count, &ring_off)) {
        if (instanced) {
            es3_functions.glDrawElementsInstanced(mode, count, GL_UNSIGNED_INT, (const void*)ring_off, instancecount);
        } else {
            es3_functions.glDrawElements(mode, count, GL_UNSIGNED_INT, (const void*)ring_off);
        }
    }
    es3_functions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_ebo);
}

static void es30_multidraw_elements_basevertex(context_t* ctx, GLenum mode, const GLsizei* count,
                                               GLenum type, const void* const* indices,
                                               GLsizei drawcount, const GLint* basevertex) {
    if (drawcount <= 0 || !count || !indices || !basevertex) return;
    GLint tbytes = type_bytes(type);
    if (tbytes == -1) return;

    /* Fast check: if all valid draws have basevertex == 0, no rebasing needed! */
    bool any_nonzero_bv = false;
    size_t total_indices = 0;
    size_t max_needed_end = 0;
    for (GLsizei i = 0; i < drawcount; i++) {
        if (count[i] <= 0) continue;
        if (basevertex[i] != 0) any_nonzero_bv = true;
        total_indices += (size_t)count[i];
        size_t end_off = (size_t)(uintptr_t)indices[i] + (size_t)count[i] * (size_t)tbytes;
        if (end_off > max_needed_end) max_needed_end = end_off;
    }
    if (total_indices == 0) return;

    if (!any_nonzero_bv) {
        for (GLsizei i = 0; i < drawcount; i++) {
            if (count[i] > 0) es3_functions.glDrawElements(mode, count[i], type, indices[i]);
        }
        return;
    }

    GLuint prev_ebo = emesis_get_current_ebo();
    const uint8_t* mirror = NULL;
    if (prev_ebo != 0) {
        mirror = emesis_es30_get_ebo_cpu_ptr(ctx, prev_ebo, max_needed_end);
        if (!mirror) return;
    }

    if (!ensure_es30_cpu_scratch(&ctx->basevertex, total_indices)) return;

    size_t cursor = 0;
    for (GLsizei i = 0; i < drawcount; i++) {
        GLsizei c = count[i];
        if (c <= 0) continue;
        const uint8_t* src_ptr = (prev_ebo != 0)
            ? (mirror + (size_t)(uintptr_t)indices[i])
            : (const uint8_t*)indices[i];
        if (!src_ptr) continue;
        rebase_indices_to_u32(ctx->basevertex.es30CpuScratch + cursor, src_ptr, c, tbytes, basevertex[i]);
        cursor += (size_t)c;
    }
    if (cursor == 0) return;

    GLintptr ring_off = 0;
    if (es30_upload_rebased_ring(ctx, ctx->basevertex.es30CpuScratch, cursor, &ring_off)) {
        if (mode == GL_TRIANGLES || mode == GL_LINES || mode == GL_POINTS) {
            /* Separable primitive topologies fuse into ONE single GPU draw call! */
            es3_functions.glDrawElements(mode, (GLsizei)cursor, GL_UNSIGNED_INT, (const void*)ring_off);
        } else {
            size_t sub_off = (size_t)ring_off;
            for (GLsizei i = 0; i < drawcount; i++) {
                if (count[i] <= 0) continue;
                es3_functions.glDrawElements(mode, count[i], GL_UNSIGNED_INT, (const void*)(uintptr_t)sub_off);
                sub_off += (size_t)count[i] * sizeof(uint32_t);
            }
        }
    }
    es3_functions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_ebo);
}

void glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                       GLsizei instancecount, GLint basevertex) {
    context_t* ctx = emesis_get_context();
    if (!ctx || count <= 0 || instancecount <= 0) return;

    if (basevertex == 0) {
        es3_functions.glDrawElementsInstanced(mode, count, type, indices, instancecount);
        return;
    }
    if (ctx->es32 && es3_functions.glDrawElementsInstancedBaseVertex) {
        es3_functions.glDrawElementsInstancedBaseVertex(mode, count, type, indices, instancecount, basevertex);
        return;
    }
    if (ctx->drawelementsbasevertex != NULL && instancecount == 1) {
        ctx->drawelementsbasevertex(mode, count, type, indices, basevertex);
        return;
    }
    GLint tbytes = type_bytes(type);
    if (tbytes == -1) return;

    if (ctx->es31 && ctx->basevertex.ready) {
        basevertex_indirect_cmd_t cmd;
        cmd.count = (GLuint)count;
        cmd.instanceCount = (GLuint)instancecount;
        cmd.firstIndex = (GLuint)((uintptr_t)indices / (uintptr_t)tbytes);
        cmd.baseVertex = basevertex;
        cmd.reservedMustBeZero = 0;
        if (emesis_dispatch_mdi_commands(ctx, mode, type, &cmd, 1)) return;
    }

    es30_draw_elements_basevertex(ctx, mode, count, type, indices, instancecount, basevertex, true);
}

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) {
    context_t* ctx = emesis_get_context();
    if (!ctx || count <= 0) return;

    if (basevertex == 0) {
        es3_functions.glDrawElements(mode, count, type, indices);
        return;
    }
    if (ctx->drawelementsbasevertex != NULL) {
        ctx->drawelementsbasevertex(mode, count, type, indices, basevertex);
        return;
    }
    GLint tbytes = type_bytes(type);
    if (tbytes == -1) return;

    if (ctx->es31 && ctx->basevertex.ready) {
        basevertex_indirect_cmd_t cmd;
        cmd.count = (GLuint)count;
        cmd.instanceCount = 1;
        cmd.firstIndex = (GLuint)((uintptr_t)indices / (uintptr_t)tbytes);
        cmd.baseVertex = basevertex;
        cmd.reservedMustBeZero = 0;
        if (emesis_dispatch_mdi_commands(ctx, mode, type, &cmd, 1)) return;
    }

    es30_draw_elements_basevertex(ctx, mode, count, type, indices, 1, basevertex, false);
}

void glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type,
                                   const void *const*indices, GLsizei drawcount, const GLint *basevertex) {
    context_t* ctx = emesis_get_context();
    if (!ctx || drawcount <= 0 || !count || !indices || !basevertex) return;

    if (drawcount == 1) {
        if (count[0] > 0) {
            glDrawElementsBaseVertex(mode, count[0], type, indices[0], basevertex[0]);
        }
        return;
    }

    /* Tier 1: Hardware glMultiDrawElementsBaseVertexEXT/OES (1 call, zero ring upload) */
    if (ctx->multidrawelementsbasevertex != NULL) {
        ctx->multidrawelementsbasevertex(mode, count, type, indices, drawcount, basevertex);
        return;
    }

    GLint tbytes = type_bytes(type);
    if (tbytes == -1) return;

    /* Tier 2: Hardware GL_EXT_multi_draw_indirect via persistent coherent ring -> 1 driver call!
     * Collapses 256-512 Sodium/Blaze3D chunk sub-draws into a single GPU command submission! */
    if (ctx->es31 && ctx->basevertex.ready && ctx->multidraw_indirect &&
        es3_functions.glMultiDrawElementsIndirectEXT &&
        emesis_get_current_ebo() != 0) {
        if (ensure_indirect_scratch(&ctx->basevertex, (size_t)drawcount)) {
            basevertex_indirect_cmd_t* cmds = ctx->basevertex.scratchPasses;
            GLsizei valid_draws = 0;
            uintptr_t shift = (tbytes == 4) ? 2u : (tbytes == 2 ? 1u : 0u);
            for (GLsizei i = 0; i < drawcount; i++) {
                GLsizei c = count[i];
                if (c <= 0) continue;
                cmds[valid_draws].count = (GLuint)c;
                cmds[valid_draws].instanceCount = 1u;
                cmds[valid_draws].firstIndex = (GLuint)(((uintptr_t)indices[i]) >> shift);
                cmds[valid_draws].baseVertex = basevertex[i];
                cmds[valid_draws].reservedMustBeZero = 0u;
                valid_draws++;
            }
            if (valid_draws == 0) return;
            if (emesis_dispatch_mdi_commands(ctx, mode, type, cmds, valid_draws)) {
                return;
            }
        }
    }

    /* Tier 3: Direct hardware glDrawElementsBaseVertex loop (ES 3.2 or OES/EXT_draw_elements_base_vertex) */
    if (ctx->drawelementsbasevertex != NULL) {
        PFNGLDRAWELEMENTSBASEVERTEXPROC fn_bv = ctx->drawelementsbasevertex;
        for (GLsizei i = 0; i < drawcount; i++) {
            GLsizei c = count[i];
            if (c <= 0) continue;
            if (basevertex[i] == 0) {
                es3_functions.glDrawElements(mode, c, type, indices[i]);
            } else {
                fn_bv(mode, c, type, indices[i], basevertex[i]);
            }
        }
        return;
    }

    /* Tier 4: ES 3.1 glDrawElementsIndirect ring fallback */
    if (ctx->es31 && ctx->basevertex.ready && emesis_get_current_ebo() != 0) {
        if (ensure_indirect_scratch(&ctx->basevertex, (size_t)drawcount)) {
            basevertex_indirect_cmd_t* cmds = ctx->basevertex.scratchPasses;
            GLsizei valid_draws = 0;
            uintptr_t shift = (tbytes == 4) ? 2u : (tbytes == 2 ? 1u : 0u);
            for (GLsizei i = 0; i < drawcount; i++) {
                GLsizei c = count[i];
                if (c <= 0) continue;
                cmds[valid_draws].count = (GLuint)c;
                cmds[valid_draws].instanceCount = 1u;
                cmds[valid_draws].firstIndex = (GLuint)(((uintptr_t)indices[i]) >> shift);
                cmds[valid_draws].baseVertex = basevertex[i];
                cmds[valid_draws].reservedMustBeZero = 0u;
                valid_draws++;
            }
            if (valid_draws == 0) return;
            if (emesis_dispatch_mdi_commands(ctx, mode, type, cmds, valid_draws)) {
                return;
            }
        }
    }

    /* Tier 5: Pure ES 3.0 zero-readback cached CPU mirror + fused single glDrawElements */
    es30_multidraw_elements_basevertex(ctx, mode, count, type, indices, drawcount, basevertex);
}

void glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count,
                                       GLsizei instancecount, GLuint baseinstance) {
    context_t* ctx = emesis_get_context();
    if (!ctx || count <= 0 || instancecount <= 0) return;
    if (baseinstance == 0) {
        es3_functions.glDrawArraysInstanced(mode, first, count, instancecount);
        return;
    }
    if (ctx->base_instance && es3_functions.glDrawArraysInstancedBaseInstanceEXT) {
        es3_functions.glDrawArraysInstancedBaseInstanceEXT(mode, first, count, instancecount, baseinstance);
    } else {
        es3_functions.glDrawArraysInstanced(mode, first, count, instancecount);
    }
}

void glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                         const void *indices, GLsizei instancecount, GLuint baseinstance) {
    context_t* ctx = emesis_get_context();
    if (!ctx || count <= 0 || instancecount <= 0) return;
    if (baseinstance == 0) {
        es3_functions.glDrawElementsInstanced(mode, count, type, indices, instancecount);
        return;
    }
    if (ctx->base_instance && es3_functions.glDrawElementsInstancedBaseInstanceEXT) {
        es3_functions.glDrawElementsInstancedBaseInstanceEXT(mode, count, type, indices, instancecount, baseinstance);
    } else {
        es3_functions.glDrawElementsInstanced(mode, count, type, indices, instancecount);
    }
}

void glDrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                                   const void *indices, GLsizei instancecount,
                                                   GLint basevertex, GLuint baseinstance) {
    context_t* ctx = emesis_get_context();
    if (!ctx || count <= 0 || instancecount <= 0) return;
    if (baseinstance == 0) {
        glDrawElementsInstancedBaseVertex(mode, count, type, indices, instancecount, basevertex);
        return;
    }
    if (ctx->base_instance && es3_functions.glDrawElementsInstancedBaseVertexBaseInstanceEXT) {
        es3_functions.glDrawElementsInstancedBaseVertexBaseInstanceEXT(
            mode, count, type, indices, instancecount, basevertex, baseinstance);
    } else {
        glDrawElementsInstancedBaseVertex(mode, count, type, indices, instancecount, basevertex);
    }
}
