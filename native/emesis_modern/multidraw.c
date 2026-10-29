/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include <stdlib.h>
#include <stdint.h>
#include "proc.h"
#include "egl.h"
#include "main.h"
#include "basevertex.h"

void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount) {
    context_t* ctx = emesis_get_context();
    if (!ctx || drawcount <= 0 || !first || !count) return;
    if (drawcount == 1) {
        if (count[0] > 0) es3_functions.glDrawArrays(mode, first[0], count[0]);
        return;
    }
    if (ctx->multidraw_ext && es3_functions.glMultiDrawArraysEXT && ctx->mda_probe_state >= 0) {
        if (ctx->mda_probe_state == 0) {
            while (es3_functions.glGetError() != GL_NO_ERROR) {}
            es3_functions.glMultiDrawArraysEXT(mode, first, count, drawcount);
            if (es3_functions.glGetError() == GL_NO_ERROR) {
                ctx->mda_probe_state = 1;
                return;
            }
            ctx->mda_probe_state = -1;
        } else {
            es3_functions.glMultiDrawArraysEXT(mode, first, count, drawcount);
            return;
        }
    }
    for (GLsizei i = 0; i < drawcount; i++) {
        if (count[i] > 0) es3_functions.glDrawArrays(mode, first[i], count[i]);
    }
}

void glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type,
                         const void *const*indices, GLsizei primcount) {
    context_t* ctx = emesis_get_context();
    if (!ctx || primcount <= 0 || !count || !indices) return;
    if (primcount == 1) {
        if (count[0] > 0) es3_functions.glDrawElements(mode, count[0], type, indices[0]);
        return;
    }
    if (ctx->multidraw_ext && es3_functions.glMultiDrawElementsEXT && ctx->mda_probe_state >= 0) {
        if (ctx->mda_probe_state == 0) {
            while (es3_functions.glGetError() != GL_NO_ERROR) {}
            es3_functions.glMultiDrawElementsEXT(mode, count, type, indices, primcount);
            if (es3_functions.glGetError() == GL_NO_ERROR) {
                ctx->mda_probe_state = 1;
                return;
            }
            ctx->mda_probe_state = -1;
        } else {
            es3_functions.glMultiDrawElementsEXT(mode, count, type, indices, primcount);
            return;
        }
    }

    GLint tbytes = type_bytes(type);
    if (tbytes != -1 && ctx->es31 && ctx->basevertex.ready &&
        ctx->multidraw_indirect && es3_functions.glMultiDrawElementsIndirectEXT &&
        ctx->mdi_probe_state >= 0 && emesis_get_current_ebo() != 0) {
        if ((size_t)primcount > ctx->basevertex.scratchCap) {
            size_t new_cap = ctx->basevertex.scratchCap ? ctx->basevertex.scratchCap : 256;
            while (new_cap < (size_t)primcount) new_cap *= 2;
            basevertex_indirect_cmd_t* p = realloc(ctx->basevertex.scratchPasses,
                                                   new_cap * sizeof(basevertex_indirect_cmd_t));
            if (p) {
                ctx->basevertex.scratchPasses = p;
                ctx->basevertex.scratchCap = new_cap;
            }
        }
        if ((size_t)primcount <= ctx->basevertex.scratchCap) {
            basevertex_indirect_cmd_t* cmds = ctx->basevertex.scratchPasses;
            GLsizei valid_draws = 0;
            uintptr_t shift = (tbytes == 4) ? 2u : (tbytes == 2 ? 1u : 0u);
            for (GLsizei i = 0; i < primcount; i++) {
                GLsizei c = count[i];
                if (c <= 0) continue;
                cmds[valid_draws].count = (GLuint)c;
                cmds[valid_draws].instanceCount = 1u;
                cmds[valid_draws].firstIndex = (GLuint)(((uintptr_t)indices[i]) >> shift);
                cmds[valid_draws].baseVertex = 0;
                cmds[valid_draws].reservedMustBeZero = 0u;
                valid_draws++;
            }
            if (valid_draws == 0) return;
            if (emesis_dispatch_mdi_commands(ctx, mode, type, cmds, valid_draws)) {
                return;
            }
        }
    }

    for (GLsizei i = 0; i < primcount; i++) {
        if (count[i] > 0) es3_functions.glDrawElements(mode, count[i], type, indices[i]);
    }
}

void glMultiDrawArraysIndirect(GLenum mode, const void *indirect, GLsizei drawcount, GLsizei stride) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !ctx->es31 || drawcount <= 0) return;
    if (ctx->multidraw_indirect && es3_functions.glMultiDrawArraysIndirectEXT && ctx->mdi_probe_state >= 0) {
        if (ctx->mdi_probe_state == 0) {
            while (es3_functions.glGetError() != GL_NO_ERROR) {}
            es3_functions.glMultiDrawArraysIndirectEXT(mode, indirect, drawcount, stride);
            if (es3_functions.glGetError() == GL_NO_ERROR) {
                ctx->mdi_probe_state = 1;
                return;
            }
            ctx->mdi_probe_state = -1;
        } else {
            es3_functions.glMultiDrawArraysIndirectEXT(mode, indirect, drawcount, stride);
            return;
        }
    }
    GLsizei step = (stride == 0) ? 16 : stride;
    uintptr_t base = (uintptr_t)indirect;
    for (GLsizei i = 0; i < drawcount; i++) {
        es3_functions.glDrawArraysIndirect(mode, (const void*)(base + (size_t)i * (size_t)step));
    }
}

void glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !ctx->es31 || drawcount <= 0) return;
    if (ctx->multidraw_indirect && es3_functions.glMultiDrawElementsIndirectEXT && ctx->mdi_probe_state >= 0) {
        if (ctx->mdi_probe_state == 0) {
            while (es3_functions.glGetError() != GL_NO_ERROR) {}
            es3_functions.glMultiDrawElementsIndirectEXT(mode, type, indirect, drawcount, stride);
            if (es3_functions.glGetError() == GL_NO_ERROR) {
                ctx->mdi_probe_state = 1;
                return;
            }
            ctx->mdi_probe_state = -1;
        } else {
            es3_functions.glMultiDrawElementsIndirectEXT(mode, type, indirect, drawcount, stride);
            return;
        }
    }
    GLsizei step = (stride == 0) ? 20 : stride;
    uintptr_t base = (uintptr_t)indirect;
    for (GLsizei i = 0; i < drawcount; i++) {
        es3_functions.glDrawElementsIndirect(mode, type, (const void*)(base + (size_t)i * (size_t)step));
    }
}
