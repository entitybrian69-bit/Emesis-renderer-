/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include <GLES3/gl31.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "proc.h"
#include "egl.h"
#include "main.h"

#define EMESIS_INDIRECT_RING_BYTES (256u * 1024u)
#define EMESIS_ES30_INDEX_RING_BYTES (512u * 1024u)

void basevertex_init(context_t* context) {
    basevertex_renderer_t *renderer = &context->basevertex;
    if(context->drawelementsbasevertex != NULL) {
        printf("EmesisRenderer: BaseVertex render calls will use host driver (%s)\n",
               context->multidrawelementsbasevertex ? "MultiDrawElementsBaseVertex" : "DrawElementsBaseVertex");
        renderer->ready = true;
        return;
    }
    if(context->es31) {
        es3_functions.glGenBuffers(1, &renderer->indirectRenderBuffer);
        if (renderer->indirectRenderBuffer != 0) {
            renderer->indirectRingSize = EMESIS_INDIRECT_RING_BYTES;
            renderer->indirectRingOffset = 0;
            es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, renderer->indirectRenderBuffer);
            es3_functions.glBufferData(GL_DRAW_INDIRECT_BUFFER, (GLsizeiptr)renderer->indirectRingSize, NULL, GL_STREAM_DRAW);
            es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            renderer->ready = true;
            printf("EmesisRenderer: BaseVertex render calls using ring-buffered ES 3.1 DrawElementsIndirect\n");
            return;
        }
    }
    /* Pure OpenGL ES 3.0 fallback (crucial for older 32-bit Android GPUs without ES 3.1 or OES_draw_elements_base_vertex) */
    es3_functions.glGenBuffers(1, &renderer->es30IndexBuffer);
    if (renderer->es30IndexBuffer != 0) {
        renderer->es30IndexRingSize = EMESIS_ES30_INDEX_RING_BYTES;
        renderer->es30IndexRingOffset = 0;
        es3_functions.glBindBuffer(GL_COPY_WRITE_BUFFER, renderer->es30IndexBuffer);
        es3_functions.glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)renderer->es30IndexRingSize, NULL, GL_STREAM_DRAW);
        es3_functions.glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        renderer->ready = true;
        printf("EmesisRenderer: BaseVertex render calls using ES 3.0 index-offset ring fallback\n");
    }
}

GLint type_bytes(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE: return 1;
        case GL_UNSIGNED_SHORT: return 2;
        case GL_UNSIGNED_INT: return 4;
        default: return -1;
    }
}

static void restore_indirect_buffer(void) {
    int idx = get_buffer_index(GL_DRAW_INDIRECT_BUFFER);
    GLuint prev = (idx >= 0) ? current_context->bound_buffers[idx] : 0;
    es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, prev);
}

static bool ensure_es30_cpu_scratch(basevertex_renderer_t* renderer, size_t count) {
    if (count <= renderer->es30CpuCap) return true;
    size_t new_cap = renderer->es30CpuCap ? renderer->es30CpuCap : 4096;
    while (new_cap < count) new_cap *= 2;
    uint32_t* p = realloc(renderer->es30CpuScratch, new_cap * sizeof(uint32_t));
    if (!p) return false;
    renderer->es30CpuScratch = p;
    renderer->es30CpuCap = new_cap;
    return true;
}

static size_t alloc_es30_ring(basevertex_renderer_t* renderer, size_t byte_len) {
    byte_len = (byte_len + 3u) & ~3u;
    if (byte_len > renderer->es30IndexRingSize) {
        size_t new_ring = renderer->es30IndexRingSize ? renderer->es30IndexRingSize : EMESIS_ES30_INDEX_RING_BYTES;
        while (new_ring < byte_len) new_ring *= 2;
        renderer->es30IndexRingSize = new_ring;
        renderer->es30IndexRingOffset = 0;
        es3_functions.glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)new_ring, NULL, GL_STREAM_DRAW);
    } else if (renderer->es30IndexRingOffset + byte_len > renderer->es30IndexRingSize) {
        es3_functions.glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)renderer->es30IndexRingSize, NULL, GL_STREAM_DRAW);
        renderer->es30IndexRingOffset = 0;
    }
    size_t off = renderer->es30IndexRingOffset;
    renderer->es30IndexRingOffset += byte_len;
    return off;
}

static void translate_indices_u32(uint32_t* dst, const uint8_t* src, GLsizei count, GLenum type, GLint basevertex) {
    if (!src) {
        memset(dst, 0, (size_t)count * sizeof(uint32_t));
        return;
    }
    if (type == GL_UNSIGNED_INT) {
        const uint32_t* s = (const uint32_t*)src;
        for (GLsizei i = 0; i < count; i++) dst[i] = (uint32_t)((int64_t)s[i] + basevertex);
    } else if (type == GL_UNSIGNED_SHORT) {
        const uint16_t* s = (const uint16_t*)src;
        for (GLsizei i = 0; i < count; i++) dst[i] = (uint32_t)((int32_t)s[i] + basevertex);
    } else {
        for (GLsizei i = 0; i < count; i++) dst[i] = (uint32_t)((int32_t)src[i] + basevertex);
    }
}

static void es30_draw_basevertex_batch(GLenum mode,
                                       const GLsizei* counts,
                                       GLenum type,
                                       const void* const* indices,
                                       GLsizei drawcount,
                                       const GLint* basevertices) {
    basevertex_renderer_t* renderer = &current_context->basevertex;
    GLint tb = type_bytes(type);
    if (tb <= 0 || renderer->es30IndexBuffer == 0) return;

    GLuint ebo = emesis_get_current_ebo();
    size_t total_indices = 0;
    uintptr_t min_off = UINTPTR_MAX;
    uintptr_t max_end = 0;
    for (GLsizei i = 0; i < drawcount; i++) {
        if (counts[i] <= 0) continue;
        total_indices += (size_t)counts[i];
        if (ebo != 0) {
            uintptr_t off = (uintptr_t)indices[i];
            uintptr_t end = off + (size_t)counts[i] * (size_t)tb;
            if (off < min_off) min_off = off;
            if (end > max_end) max_end = end;
        }
    }
    if (total_indices == 0) return;
    if (!ensure_es30_cpu_scratch(renderer, total_indices)) return;

    const uint8_t* mapped_base = NULL;
    if (ebo != 0) {
        if (max_end <= min_off) return;
        mapped_base = (const uint8_t*)es3_functions.glMapBufferRange(
            GL_ELEMENT_ARRAY_BUFFER, (GLintptr)min_off, (GLsizeiptr)(max_end - min_off), GL_MAP_READ_BIT);
        if (!mapped_base) return;
    }

    size_t cursor = 0;
    for (GLsizei i = 0; i < drawcount; i++) {
        GLsizei c = counts[i];
        if (c <= 0) continue;
        const uint8_t* src = (ebo != 0)
            ? (mapped_base + ((uintptr_t)indices[i] - min_off))
            : (const uint8_t*)indices[i];
        translate_indices_u32(renderer->es30CpuScratch + cursor, src, c, type, basevertices[i]);
        cursor += (size_t)c;
    }

    if (ebo != 0) {
        es3_functions.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    }

    size_t byte_len = total_indices * sizeof(uint32_t);
    es3_functions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->es30IndexBuffer);
    size_t ring_off = alloc_es30_ring(renderer, byte_len);
    es3_functions.glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)ring_off, (GLsizeiptr)byte_len, renderer->es30CpuScratch);

    bool mergeable = (mode == GL_TRIANGLES || mode == GL_LINES || mode == GL_POINTS);
    if (mergeable || drawcount == 1) {
        es3_functions.glDrawElements(mode, (GLsizei)total_indices, GL_UNSIGNED_INT, (const void*)(uintptr_t)ring_off);
    } else {
        size_t sub_off = ring_off;
        for (GLsizei i = 0; i < drawcount; i++) {
            if (counts[i] <= 0) continue;
            es3_functions.glDrawElements(mode, counts[i], GL_UNSIGNED_INT, (const void*)(uintptr_t)sub_off);
            sub_off += (size_t)counts[i] * sizeof(uint32_t);
        }
    }

    es3_functions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
}

static size_t upload_indirect_ring(basevertex_renderer_t* renderer, const basevertex_indirect_cmd_t* cmds, size_t cmd_count) {
    size_t byte_len = cmd_count * sizeof(basevertex_indirect_cmd_t);
    es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, renderer->indirectRenderBuffer);
    if (byte_len > renderer->indirectRingSize) {
        size_t new_ring = renderer->indirectRingSize ? renderer->indirectRingSize : EMESIS_INDIRECT_RING_BYTES;
        while (new_ring < byte_len) new_ring *= 2;
        renderer->indirectRingSize = new_ring;
        renderer->indirectRingOffset = 0;
        es3_functions.glBufferData(GL_DRAW_INDIRECT_BUFFER, (GLsizeiptr)new_ring, NULL, GL_STREAM_DRAW);
    } else if (renderer->indirectRingOffset + byte_len > renderer->indirectRingSize) {
        es3_functions.glBufferData(GL_DRAW_INDIRECT_BUFFER, (GLsizeiptr)renderer->indirectRingSize, NULL, GL_STREAM_DRAW);
        renderer->indirectRingOffset = 0;
    }
    size_t off = renderer->indirectRingOffset;
    renderer->indirectRingOffset += byte_len;
    es3_functions.glBufferSubData(GL_DRAW_INDIRECT_BUFFER, (GLintptr)off, (GLsizeiptr)byte_len, cmds);
    return off;
}

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) {
    emesis_ensure_current_context();
    if(!current_context || count <= 0) return;
    if(basevertex == 0) {
        es3_functions.glDrawElements(mode, count, type, indices);
        return;
    }
    if(current_context->drawelementsbasevertex != NULL) {
        current_context->drawelementsbasevertex(mode, count, type, indices, basevertex);
        return;
    }
    basevertex_renderer_t *renderer = &current_context->basevertex;
    if(!renderer->ready) return;
    if(!current_context->es31 || renderer->indirectRenderBuffer == 0) {
        es30_draw_basevertex_batch(mode, &count, type, &indices, 1, &basevertex);
        return;
    }
    GLuint elementbuffer = emesis_get_current_ebo();
    if(elementbuffer == 0) {
        es30_draw_basevertex_batch(mode, &count, type, &indices, 1, &basevertex);
        return;
    }
    GLint typeBytes = type_bytes(type);
    if (typeBytes <= 0) return;
    uintptr_t indicesPointer = (uintptr_t)indices;
    if(indicesPointer % (uintptr_t)typeBytes != 0) {
        es30_draw_basevertex_batch(mode, &count, type, &indices, 1, &basevertex);
        return;
    }
    basevertex_indirect_cmd_t cmd;
    cmd.count = (GLuint)count;
    cmd.instanceCount = 1;
    cmd.firstIndex = (GLuint)(indicesPointer / (uintptr_t)typeBytes);
    cmd.baseVertex = basevertex;
    cmd.reservedMustBeZero = 0;
    size_t ring_off = upload_indirect_ring(renderer, &cmd, 1);
    es3_functions.glDrawElementsIndirect(mode, type, (const void*)(uintptr_t)ring_off);
    restore_indirect_buffer();
}

void glMultiDrawElementsBaseVertex(GLenum mode,
                                   const GLsizei *count,
                                   GLenum type,
                                   const void * const *indices,
                                   GLsizei drawcount,
                                   const GLint *basevertex) {
    emesis_ensure_current_context();
    if(!current_context || drawcount <= 0 || !count || !indices || !basevertex) return;
    if(current_context->multidrawelementsbasevertex != NULL) {
        current_context->multidrawelementsbasevertex(mode, count, type, indices, drawcount, basevertex);
        return;
    }
    if(current_context->drawelementsbasevertex != NULL) {
        for(GLsizei i = 0; i < drawcount; i++) {
            if (count[i] <= 0) continue;
            if (basevertex[i] == 0) {
                es3_functions.glDrawElements(mode, count[i], type, indices[i]);
            } else {
                current_context->drawelementsbasevertex(mode, count[i], type, indices[i], basevertex[i]);
            }
        }
        return;
    }
    basevertex_renderer_t *renderer = &current_context->basevertex;
    if(!renderer->ready) return;
    if(!current_context->es31 || renderer->indirectRenderBuffer == 0) {
        es30_draw_basevertex_batch(mode, count, type, indices, drawcount, basevertex);
        return;
    }
    GLuint elementbuffer = emesis_get_current_ebo();
    if(elementbuffer == 0) {
        es30_draw_basevertex_batch(mode, count, type, indices, drawcount, basevertex);
        return;
    }
    GLint typeBytes = type_bytes(type);
    if (typeBytes <= 0) return;
    if ((size_t)drawcount > renderer->scratchCap) {
        size_t new_cap = renderer->scratchCap ? renderer->scratchCap : 256;
        while (new_cap < (size_t)drawcount) new_cap *= 2;
        basevertex_indirect_cmd_t* p = realloc(renderer->scratchPasses, new_cap * sizeof(basevertex_indirect_cmd_t));
        if (!p) return;
        renderer->scratchPasses = p;
        renderer->scratchCap = new_cap;
    }
    GLsizei valid = 0;
    for(GLsizei i = 0; i < drawcount; i++) {
        if (count[i] <= 0) continue;
        uintptr_t indicesPointer = (uintptr_t)indices[i];
        if(indicesPointer % (uintptr_t)typeBytes != 0) {
            es30_draw_basevertex_batch(mode, count, type, indices, drawcount, basevertex);
            return;
        }
        basevertex_indirect_cmd_t* pass = &renderer->scratchPasses[valid++];
        pass->count = (GLuint)count[i];
        pass->instanceCount = 1;
        pass->firstIndex = (GLuint)(indicesPointer / (uintptr_t)typeBytes);
        pass->baseVertex = basevertex[i];
        pass->reservedMustBeZero = 0;
    }
    if (valid == 0) return;
    size_t ring_off = upload_indirect_ring(renderer, renderer->scratchPasses, (size_t)valid);
    if(current_context->multidraw_indirect && es3_functions.glMultiDrawElementsIndirectEXT) {
        es3_functions.glMultiDrawElementsIndirectEXT(mode, type, (const void*)(uintptr_t)ring_off, valid, 0);
    } else {
        for(GLsizei i = 0; i < valid; i++) {
            es3_functions.glDrawElementsIndirect(mode, type,
                (const void*)(uintptr_t)(ring_off + (size_t)i * sizeof(basevertex_indirect_cmd_t)));
        }
    }
    restore_indirect_buffer();
}
