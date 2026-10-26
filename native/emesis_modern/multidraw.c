/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include <stdint.h>
#include <proc.h>
#include <egl.h>
#include "basevertex.h"

void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) {
    emesis_ensure_current_context();
    if(!current_context || primcount <= 0 || !first || !count) return;
    if(es3_functions.glMultiDrawArraysEXT != NULL) {
        es3_functions.glMultiDrawArraysEXT(mode, first, count, primcount);
        return;
    }
    for (GLsizei i = 0; i < primcount; i++) {
        if (count[i] > 0)
            es3_functions.glDrawArrays(mode, first[i], count[i]);
    }
}

void glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void * const *indices, GLsizei primcount) {
    emesis_ensure_current_context();
    if(!current_context || primcount <= 0 || !count || !indices) return;
    if(es3_functions.glMultiDrawElementsEXT != NULL) {
        es3_functions.glMultiDrawElementsEXT(mode, count, type, indices, primcount);
        return;
    }
    /* Direct per-subdraw submission avoids glGetIntegerv sync stalls, EBO reallocation,
     * glCopyBufferSubData overhead, and VAO GL_ELEMENT_ARRAY_BUFFER state clobbering. */
    for (GLsizei i = 0; i < primcount; i++) {
        if (count[i] > 0)
            es3_functions.glDrawElements(mode, count[i], type, indices[i]);
    }
}

void glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride) {
    emesis_ensure_current_context();
    if(!current_context || drawcount <= 0) return;
    if(current_context->multidraw_indirect && es3_functions.glMultiDrawElementsIndirectEXT != NULL) {
        es3_functions.glMultiDrawElementsIndirectEXT(mode, type, indirect, drawcount, stride);
        return;
    }
    if(current_context->es31 && es3_functions.glDrawElementsIndirect != NULL) {
        size_t step = (stride != 0) ? (size_t)stride : 20u;
        uintptr_t base = (uintptr_t)indirect;
        for (GLsizei i = 0; i < drawcount; i++) {
            es3_functions.glDrawElementsIndirect(mode, type, (const void*)(base + (size_t)i * step));
        }
    }
}

void glMultiDrawArraysIndirect(GLenum mode, const void *indirect, GLsizei drawcount, GLsizei stride) {
    emesis_ensure_current_context();
    if(!current_context || drawcount <= 0) return;
    if(current_context->multidraw_indirect && es3_functions.glMultiDrawArraysIndirectEXT != NULL) {
        es3_functions.glMultiDrawArraysIndirectEXT(mode, indirect, drawcount, stride);
        return;
    }
    if(current_context->es31 && es3_functions.glDrawArraysIndirect != NULL) {
        size_t step = (stride != 0) ? (size_t)stride : 16u;
        uintptr_t base = (uintptr_t)indirect;
        for (GLsizei i = 0; i < drawcount; i++) {
            es3_functions.glDrawArraysIndirect(mode, (const void*)(base + (size_t)i * step));
        }
    }
}