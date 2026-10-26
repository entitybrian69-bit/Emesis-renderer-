#include <stdint.h>
#include "../glx/hardext.h"
#include "array.h"
#include "gl4es.h"
#include "glcase.h"
#include "enum_info.h"
#include "fpe.h"
#include "gles.h"
#include "glstate.h"
#include "init.h"
#include "list.h"
#include "loader.h"
#include "logs.h"
#include "render.h"

// #define DEBUG
#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

static GLboolean is_cache_compatible(GLsizei count) {
#define T2(AA, A, B)                                                                                                   \
    if (glstate->vao->AA != glstate->vao->B.enabled) return GL_FALSE;                                                  \
    if (glstate->vao->B.enabled &&                                                                                     \
        memcmp(&glstate->vao->vertexattrib[A], &glstate->vao->B.state, sizeof(vertexattrib_t)))                        \
        return GL_FALSE;
#define TEST(A, B) T2(vertexattrib[A].enabled, A, B)
#define TESTA(A, B, I) T2(vertexattrib[A + i].enabled, A + i, B[i])

    if (glstate->vao == glstate->defaultvao) return GL_FALSE;
    if (count > glstate->vao->cache_count) return GL_FALSE;
    TEST(ATT_VERTEX, vert)
    TEST(ATT_COLOR, color)
    TEST(ATT_SECONDARY, secondary)
    TEST(ATT_FOGCOORD, fog)
    TEST(ATT_NORMAL, normal)
    for (int i = 0; i < hardext.maxtex; i++) {
        TESTA(ATT_MULTITEXCOORD0, tex, i)
    }
#undef TESTA
#undef TEST
#undef T2
    return GL_TRUE;
}

GLboolean is_list_compatible(renderlist_t* list) {
#define T2(AA, A, B)                                                                                                   \
    if (glstate->vao->AA != (list->B != NULL)) return GL_FALSE;
#define TEST(A, B) T2(vertexattrib[A].enabled, A, B)
#define TESTA(A, B, I) T2(vertexattrib[A + i].enabled, A + i, B[i])

    if (list->post_color && !list->color) return GL_FALSE;
    if (list->post_normal && !list->normal) return GL_FALSE;
    TEST(ATT_VERTEX, vert)
    TEST(ATT_COLOR, color)
    TEST(ATT_SECONDARY, secondary)
    TEST(ATT_FOGCOORD, fogcoord)
    TEST(ATT_NORMAL, normal)
    for (int i = 0; i < hardext.maxtex; i++) {
        TESTA(ATT_MULTITEXCOORD0, tex, i)
    }
#undef TESTA
#undef TEST
#undef T2
    return GL_TRUE;
}

renderlist_t* arrays_to_renderlist(renderlist_t* list, GLenum mode, GLsizei skip, GLsizei count) {
    if (!list) list = alloc_renderlist();
    DBG(LOGD("arrary_to_renderlist, compiling=%d, skip=%d, count=%d\n", glstate->list.compiling, skip, count);)
    list->mode = mode;
    list->mode_init = mode;
    list->mode_dimension = rendermode_dimensions(mode);
    list->len = count - skip;
    list->cap = count - skip;

    // check cache if any
    if (glstate->vao->shared_arrays) {
        if (!is_cache_compatible(count)) VaoSharedClear(glstate->vao);
    }

    if (glstate->vao->shared_arrays) {
#define OP(A, N) (A) ? (A + skip * N) : NULL
        list->vert = OP(glstate->vao->vert.ptr, 4);
        list->color = OP(glstate->vao->color.ptr, 4);
        list->secondary = OP(glstate->vao->secondary.ptr, 4);
        list->fogcoord = OP(glstate->vao->fog.ptr, 1);
        list->normal = OP(glstate->vao->normal.ptr, 3);
        for (int i = 0; i < hardext.maxtex; i++)
            list->tex[i] = OP(glstate->vao->tex[i].ptr, 4);
#undef OP

        list->shared_arrays = glstate->vao->shared_arrays;
        (*glstate->vao->shared_arrays)++;
    } else {
        if (!globals4es.novaocache && glstate->vao != glstate->defaultvao) {
            // prepare a vao cache object
            list->shared_arrays = glstate->vao->shared_arrays = (int*)malloc(sizeof(int));
            *glstate->vao->shared_arrays = 2; // already shared between glstate & list
#define G2(AA, A, B)                                                                                                   \
    glstate->vao->B.enabled = glstate->vao->vertexattrib[AA].enabled;                                                  \
    if (glstate->vao->B.enabled) memcpy(&glstate->vao->B.state, &glstate->vao->vertexattrib[A], sizeof(vertexattrib_t));
#define GO(A, B) G2(A, A, B)
#define GOA(A, B, I) G2(A + i, A + i, B[i])
            GO(ATT_VERTEX, vert)
            GO(ATT_COLOR, color)
            GO(ATT_SECONDARY, secondary)
            GO(ATT_FOGCOORD, fog)
            GO(ATT_NORMAL, normal)
            for (int i = 0; i < hardext.maxtex; i++) {
                GOA(ATT_MULTITEXCOORD0, tex, i)
            }
            glstate->vao->cache_count = count;
#undef GOA
#undef GO
#undef G2
        }
        if (glstate->vao->vertexattrib[ATT_VERTEX].enabled) {
            if (glstate->vao->shared_arrays) {
                glstate->vao->vert.ptr = copy_gl_pointer_tex(&glstate->vao->vertexattrib[ATT_VERTEX], 4, 0, count);
                list->vert = glstate->vao->vert.ptr + 4 * skip;
            } else
                list->vert = copy_gl_pointer_tex(&glstate->vao->vertexattrib[ATT_VERTEX], 4, skip, count);
        }
        if (glstate->vao->vertexattrib[ATT_COLOR].enabled) {
            vertexattrib_t* ca = &glstate->vao->vertexattrib[ATT_COLOR];
            const void* ca_ptr = ca->pointer;
            if (ca->size == GL_BGRA && ca->buffer) {
                if (!ca->buffer->data) emesis_ensure_buffer_cpu_shadow(ca->buffer, GL_ARRAY_BUFFER, 1);
                ca_ptr = (const char*)ca->buffer->data + (uintptr_t)ca->pointer;
            }
            if (glstate->vao->shared_arrays) {
                if (ca->size == GL_BGRA)
                    glstate->vao->color.ptr = copy_gl_pointer_color_bgra(ca_ptr, ca->stride, 4, 0, count);
                else
                    glstate->vao->color.ptr = copy_gl_pointer_color(ca, 4, 0, count);
                list->color = glstate->vao->color.ptr + 4 * skip;
            } else {
                if (ca->size == GL_BGRA)
                    list->color = copy_gl_pointer_color_bgra(ca_ptr, ca->stride, 4, skip, count);
                else
                    list->color = copy_gl_pointer_color(ca, 4, skip, count);
            }
        }
        if (glstate->vao->vertexattrib[ATT_SECONDARY].enabled /* && glstate->enable.color_array*/) {
            vertexattrib_t* sa = &glstate->vao->vertexattrib[ATT_SECONDARY];
            const void* sa_ptr = sa->pointer;
            if (sa->size == GL_BGRA && sa->buffer) {
                if (!sa->buffer->data) emesis_ensure_buffer_cpu_shadow(sa->buffer, GL_ARRAY_BUFFER, 1);
                sa_ptr = (const char*)sa->buffer->data + (uintptr_t)sa->pointer;
            }
            if (glstate->vao->shared_arrays) {
                if (sa->size == GL_BGRA)
                    glstate->vao->secondary.ptr = copy_gl_pointer_color_bgra(sa_ptr, sa->stride, 4, 0, count);
                else
                    glstate->vao->secondary.ptr = copy_gl_pointer(sa, 4, 0, count);
                list->secondary = glstate->vao->secondary.ptr + 4 * skip;
            } else {
                if (sa->size == GL_BGRA)
                    list->secondary = copy_gl_pointer_color_bgra(sa_ptr, sa->stride, 4, skip, count);
                else
                    list->secondary = copy_gl_pointer(sa, 4, skip, count);
            }
        }
        if (glstate->vao->vertexattrib[ATT_NORMAL].enabled) {
            if (glstate->vao->shared_arrays) {
                glstate->vao->normal.ptr = copy_gl_pointer_raw(&glstate->vao->vertexattrib[ATT_NORMAL], 3, 0, count);
                list->normal = glstate->vao->normal.ptr + 3 * skip;
            } else
                list->normal = copy_gl_pointer_raw(&glstate->vao->vertexattrib[ATT_NORMAL], 3, skip, count);
        }
        if (glstate->vao->vertexattrib[ATT_FOGCOORD].enabled) {
            if (glstate->vao->shared_arrays) {
                glstate->vao->fog.ptr = copy_gl_pointer_raw(&glstate->vao->vertexattrib[ATT_FOGCOORD], 1, 0, count);
                list->fogcoord = glstate->vao->fog.ptr + 1 * skip;
            } else
                list->fogcoord = copy_gl_pointer_raw(&glstate->vao->vertexattrib[ATT_FOGCOORD], 1, skip, count);
        }
        for (int i = 0; i < glstate->vao->maxtex; i++) {
            if (glstate->vao->vertexattrib[ATT_MULTITEXCOORD0 + i].enabled) {
                if (glstate->vao->shared_arrays) {
                    glstate->vao->tex[i].ptr =
                        copy_gl_pointer_tex(&glstate->vao->vertexattrib[ATT_MULTITEXCOORD0 + i], 4, 0, count);
                    list->tex[i] = glstate->vao->tex[i].ptr + 4 * skip;
                } else
                    list->tex[i] =
                        copy_gl_pointer_tex(&glstate->vao->vertexattrib[ATT_MULTITEXCOORD0 + i], 4, skip, count);
            }
        }
    }
    for (int i = 0; i < hardext.maxtex; i++)
        if (list->tex[i] && list->maxtex < i + 1) list->maxtex = i + 1;
    return list;
}
static renderlist_t* arrays_add_renderlist(renderlist_t* a, GLenum mode, GLsizei skip, GLsizei count, GLushort* indices,
                                           int ilen_b) {
    DBG(LOGD("arrays_add_renderlist(%p, %s, %d, %d, %p, %d)\n", a, PrintEnum(mode), skip, count, indices, ilen_b);)
    // check cache if any
    if (glstate->vao->shared_arrays) {
        if (!is_cache_compatible(count)) VaoSharedClear(glstate->vao);
    }
    // append all draw elements of b in a
    // check the final indice size of a and b
    int ilen_a = a->ilen;
    int len_b = count - skip;
    // lets append all the arrays
    unsigned long cap = a->cap;
    if (a->len + len_b >= cap) cap += len_b + DEFAULT_RENDER_LIST_CAPACITY;
    // Unshare if shared (shared array are not used for now)
    unshared_renderlist(a, cap);
    redim_renderlist(a, cap);
    unsharedindices_renderlist(a, ((ilen_a) ? ilen_a : a->len) + ((ilen_b) ? ilen_b : len_b));
    // append arrays
    if (glstate->vao->shared_arrays) {
        if (a->vert) memcpy(a->vert + a->len * 4, glstate->vao->vert.ptr + skip * 4, len_b * 4 * sizeof(GLfloat));
        if (a->normal) memcpy(a->normal + a->len * 3, glstate->vao->normal.ptr + skip * 3, len_b * 3 * sizeof(GLfloat));
        if (a->color) memcpy(a->color + a->len * 4, glstate->vao->color.ptr + skip * 4, len_b * 4 * sizeof(GLfloat));
        if (a->secondary)
            memcpy(a->secondary + a->len * 4, glstate->vao->secondary.ptr + skip * 4, len_b * 4 * sizeof(GLfloat));
        if (a->fogcoord)
            memcpy(a->fogcoord + a->len * 1, glstate->vao->fog.ptr + skip * 1, len_b * 1 * sizeof(GLfloat));
        for (int i = 0; i < a->maxtex; i++)
            if (a->tex[i])
                memcpy(a->tex[i] + a->len * 4, glstate->vao->tex[i].ptr + skip * 4, len_b * 4 * sizeof(GLfloat));
    } else {
        if (a->vert)
            copy_gl_pointer_tex_noalloc(a->vert + a->len * 4, &glstate->vao->vertexattrib[ATT_VERTEX], 4, skip, count);
        if (a->normal)
            copy_gl_pointer_raw_noalloc(a->normal + a->len * 3, &glstate->vao->vertexattrib[ATT_NORMAL], 3, skip,
                                        count);
        if (a->color) {
            vertexattrib_t* ca = &glstate->vao->vertexattrib[ATT_COLOR];
            if (ca->size == GL_BGRA) {
                const void* ca_ptr = ca->pointer;
                if (ca->buffer) {
                    if (!ca->buffer->data) emesis_ensure_buffer_cpu_shadow(ca->buffer, GL_ARRAY_BUFFER, 1);
                    ca_ptr = (const char*)ca->buffer->data + (uintptr_t)ca->pointer;
                }
                copy_gl_pointer_color_bgra_noalloc(a->color + a->len * 4, ca_ptr, ca->stride, 4, skip, count);
            } else
                copy_gl_pointer_color_noalloc(a->color + a->len * 4, ca, 4, skip, count);
        }
        if (a->secondary) {
            vertexattrib_t* sa = &glstate->vao->vertexattrib[ATT_SECONDARY];
            if (sa->size == GL_BGRA) {
                const void* sa_ptr = sa->pointer;
                if (sa->buffer) {
                    if (!sa->buffer->data) emesis_ensure_buffer_cpu_shadow(sa->buffer, GL_ARRAY_BUFFER, 1);
                    sa_ptr = (const char*)sa->buffer->data + (uintptr_t)sa->pointer;
                }
                copy_gl_pointer_color_bgra_noalloc(a->secondary + a->len * 4, sa_ptr, sa->stride, 4, skip, count);
            } else
                copy_gl_pointer_noalloc(a->secondary + a->len * 4, sa, 4, skip, count);
        }
        if (a->fogcoord)
            copy_gl_pointer_raw_noalloc(a->fogcoord + a->len * 1, &glstate->vao->vertexattrib[ATT_FOGCOORD], 1, skip,
                                        count);
        for (int i = 0; i < a->maxtex; i++)
            if (a->tex[i])
                copy_gl_pointer_tex_noalloc(a->tex[i] + a->len * 4, &glstate->vao->vertexattrib[ATT_MULTITEXCOORD0 + i],
                                            4, skip, count);
    }
    // indices
    int old_ilenb = ilen_b;
    if (!a->mode_inits) list_add_modeinit(a, a->mode_init);
    if (ilen_a || ilen_b || mode_needindices(a->mode) || mode_needindices(mode) ||
        (a->mode != mode && (a->mode == GL_QUADS || mode == GL_QUADS))) {
        // alloc or realloc a->indices first...
        ilen_b = indices_getindicesize(mode, ((indices) ? ilen_b : len_b));
        prepareadd_renderlist(a, ilen_b);
        // then append b
        doadd_renderlist(a, mode, indices, indices ? old_ilenb : len_b, ilen_b);
    }
    // lenghts
    a->len += len_b;
    if (a->mode_inits) list_add_modeinit(a, mode);
    // all done
    a->stage = STAGE_DRAW; // just in case
    return a;
}

static inline bool should_intercept_render(GLenum mode) {
    // check bounded tex that will be used if one need some transformations
    if (hardext.esversion == 1) // but only for ES1.1
        for (int aa = 0; aa < hardext.maxtex; aa++) {
            if (glstate->enable.texture[aa]) {
                if ((hardext.esversion == 1) && ((glstate->enable.texgen_s[aa] || glstate->enable.texgen_t[aa] ||
                                                  glstate->enable.texgen_r[aa] || glstate->enable.texgen_q[aa])))
                    return true;
                if ((!glstate->vao->vertexattrib[ATT_MULTITEXCOORD0 + aa].enabled) &&
                    !(mode == GL_POINT && glstate->texture.pscoordreplace[aa]))
                    return true;
                if ((glstate->vao->vertexattrib[ATT_MULTITEXCOORD0 + aa].enabled) &&
                    (glstate->vao->vertexattrib[ATT_MULTITEXCOORD0 + aa].size == 1))
                    return true;
            }
        }
    if (glstate->polygon_mode == GL_LINE && mode >= GL_TRIANGLES) return true;
    if ((hardext.esversion == 1) &&
        ((glstate->vao->vertexattrib[ATT_SECONDARY].enabled) && (glstate->vao->vertexattrib[ATT_COLOR].enabled)))
        return true;
    if ((hardext.esversion == 1) &&
        (glstate->vao->vertexattrib[ATT_COLOR].enabled && (glstate->vao->vertexattrib[ATT_COLOR].size != 4)))
        return true;
    // if (glstate->vao->vertex || glstate->vao->elements)
    //     return false;   // don't try to intercept VAO
    return ((glstate->vao->vertexattrib[ATT_VERTEX].enabled &&
             !valid_vertex_type(glstate->vao->vertexattrib[ATT_VERTEX].type)) ||
            (mode == GL_LINES && glstate->enable.line_stipple) ||
            /*(mode == GL_QUADS) ||*/ (glstate->list.active && !glstate->list.pending));
}

GLuint len_indices(const GLushort* sindices, const GLuint* iindices, GLsizei count) {
    GLuint len = 0;
    if (sindices) {
        for (int i = 0; i < count; i++)
            if (len < sindices[i]) len = sindices[i]; // get the len of the arrays
    } else {
        for (int i = 0; i < count; i++)
            if (len < iindices[i]) len = iindices[i]; // get the len of the arrays
    }
    return len + 1; // length is max(indices) + 1 !
}

static void glDrawElementsCommon(GLenum mode, GLint first, GLsizei count, GLuint len, const GLushort* sindices,
                                 const GLuint* iindices, int instancecount) {
    if (glstate->raster.bm_drawing) bitmap_flush();
    DBG(SHUT_LOGD("glDrawElementsCommon(%s, %d, %d, %d, %p, %p, %d)\n", PrintEnum(mode), first, count, len, sindices,
                  iindices, instancecount);)
    LOAD_GLES_FPE(glDrawElements);
    LOAD_GLES_FPE(glDrawArrays);
    LOAD_GLES_FPE(glNormalPointer);
    LOAD_GLES_FPE(glVertexPointer);
    LOAD_GLES_FPE(glColorPointer);
    LOAD_GLES_FPE(glTexCoordPointer);
    LOAD_GLES_FPE(glEnable);
    LOAD_GLES_FPE(glDisable);
    LOAD_GLES_FPE(glMultiTexCoord4f);
#define client_state(A, B, C)                                                                                          \
    if ((glstate->vao->vertexattrib[A].enabled != glstate->gleshard->vertexattrib[A].enabled) ||                       \
        (hardext.esversion != 1)) {                                                                                    \
        C if (glstate->vao->vertexattrib[A].enabled) fpe_glEnableClientState(B);                                       \
        else fpe_glDisableClientState(B);                                                                              \
    }
#if 0
// FEZ draw the stars (intro menu and the ones visible by night)
// by drawing a huge list of 500k+ triangles!
// it's a bit too much for mobile hardware, so it can be simply disabled here
if(count>500000) return;
#endif
    GLenum mode_init = mode;
    /*if (glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
        mode = GL_LINE_LOOP;*/
    if (glstate->polygon_mode == GL_POINT && mode >= GL_TRIANGLES) mode = GL_POINTS;

    if (mode == GL_QUAD_STRIP) mode = GL_TRIANGLE_STRIP;
    if (mode == GL_POLYGON) mode = GL_TRIANGLE_FAN;
    if (mode == GL_QUADS) {
        mode = GL_TRIANGLES;
        int ilen = (count * 3) / 2;
        if (iindices) {
            gl4es_scratch(ilen * sizeof(GLuint));
            GLuint* tmp = (GLuint*)glstate->scratch;
            for (int i = 0, j = 0; i + 3 < count; i += 4, j += 6) {
                tmp[j + 0] = iindices[i + 0];
                tmp[j + 1] = iindices[i + 1];
                tmp[j + 2] = iindices[i + 2];

                tmp[j + 3] = iindices[i + 0];
                tmp[j + 4] = iindices[i + 2];
                tmp[j + 5] = iindices[i + 3];
            }
            iindices = tmp;
        } else {
            gl4es_scratch(ilen * sizeof(GLushort));
            GLushort* tmp = (GLushort*)glstate->scratch;
            for (int i = 0, j = 0; i + 3 < count; i += 4, j += 6) {
                tmp[j + 0] = sindices[i + 0];
                tmp[j + 1] = sindices[i + 1];
                tmp[j + 2] = sindices[i + 2];

                tmp[j + 3] = sindices[i + 0];
                tmp[j + 4] = sindices[i + 2];
                tmp[j + 5] = sindices[i + 3];
            }
            sindices = tmp;
        }
        count = ilen;
    }
    // of course, GL_SELECT with shader will just not work if not using standard transformation method... Instance count
    // is ignored also
    if (glstate->render_mode == GL_SELECT) {
        // TODO handling uint indices
        if (!sindices && !iindices)
            select_glDrawArrays(&glstate->vao->vertexattrib[ATT_VERTEX], mode, first, count);
        else
            select_glDrawElements(&glstate->vao->vertexattrib[ATT_VERTEX], mode, count,
                                  sindices ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                  sindices ? ((void*)sindices) : ((void*)iindices));
    } else {
        GLuint old_tex = glstate->texture.client;

        realize_textures(1);
        if (hardext.esversion == 1) {

#define TEXTURE(A) gl4es_glClientActiveTexture(A + GL_TEXTURE0);

            vertexattrib_t* p;
#define GetP(A) (&glstate->vao->vertexattrib[A])
            // secondary color and color sizef != 4 are "intercepted" and draw using a list, unless using ES>1.1
            client_state(ATT_COLOR, GL_COLOR_ARRAY, );
            p = GetP(ATT_COLOR);
            if (p->enabled) gles_glColorPointer(p->size, p->type, p->stride, p->pointer);
            client_state(ATT_NORMAL, GL_NORMAL_ARRAY, );
            p = GetP(ATT_NORMAL);
            if (p->enabled) gles_glNormalPointer(p->type, p->stride, p->pointer);
            client_state(ATT_VERTEX, GL_VERTEX_ARRAY, );
            p = GetP(ATT_VERTEX);
            if (p->enabled) gles_glVertexPointer(p->size, p->type, p->stride, p->pointer);
            for (int aa = 0; aa < hardext.maxtex; aa++) {
                client_state(ATT_MULTITEXCOORD0 + aa, GL_TEXTURE_COORD_ARRAY, TEXTURE(aa););
                p = GetP(ATT_MULTITEXCOORD0 + aa);
                // get 1st enabled target
                const GLint itarget = get_target(glstate->enable.texture[aa]);
                if (itarget >= 0) {
                    if (!IS_TEX2D(glstate->enable.texture[aa]) && (IS_ANYTEX(glstate->enable.texture[aa]))) {
                        gl4es_glActiveTexture(GL_TEXTURE0 + aa);
                        realize_active();
                        gles_glEnable(GL_TEXTURE_2D);
                    }
                    if (p->enabled) {
                        TEXTURE(aa);
                        int changes = tex_setup_needchange(itarget);
                        if (changes && !len) len = len_indices(sindices, iindices, count);
                        tex_setup_texcoord(len, changes, itarget, p);
                    } else
                        gles_glMultiTexCoord4f(GL_TEXTURE0 + aa, glstate->texcoord[aa][0], glstate->texcoord[aa][1],
                                               glstate->texcoord[aa][2], glstate->texcoord[aa][3]);
                }
            }
#undef GetP
            if (glstate->texture.client != old_tex) TEXTURE(old_tex);
        }
        // check if arrays are locked and can be put in a VBO
        if (hardext.esversion > 1 && globals4es.usevbo == 2 && glstate->vao->locked == 1) {
            // can now browse all enabled VA, and put the corresponding data in a VBO
            // warning, with the use of first
            // Checking only Vertex Attrib for now!
            // TODO: check all va, and take care of interleaved ones...
            ToBuffer(glstate->vao->first, glstate->vao->count);
        }
        if (hardext.esversion > 1 && globals4es.usevbo == 3 &&
            (glstate->vao->locked == 1 || glstate->vao->locked == 2)) {
            if (glstate->vao->locked == 1)
                glstate->vao->locked++;
            else
                ToBuffer(glstate->vao->first, glstate->vao->count);
        }

        // POLYGON mode as LINE is "intercepted" and drawn using list
        if (instancecount == 1 || hardext.esversion == 1) {
            if (!iindices && !sindices)
                gles_glDrawArrays(mode, first, count);
            else
                gles_glDrawElements(mode, count, (sindices) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                    (sindices ? ((void*)sindices) : ((void*)iindices)));
        } else {
            if (!iindices && !sindices)
                fpe_glDrawArraysInstanced(mode, first, count, instancecount);
            else {
                void* tmp = (sindices ? ((void*)sindices) : ((void*)iindices));
                GLenum t = (sindices) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
                fpe_glDrawElementsInstanced(mode, count, t, tmp, instancecount);
            }
        }

        if (hardext.esversion == 1) {
            for (int aa = 0; aa < hardext.maxtex; aa++) {
                if (!IS_TEX2D(glstate->enable.texture[aa]) && (IS_ANYTEX(glstate->enable.texture[aa]))) {
                    gl4es_glActiveTexture(GL_TEXTURE0 + aa);
                    realize_active();
                    gles_glDisable(GL_TEXTURE_2D);
                }
            }
            if (glstate->texture.client != old_tex) TEXTURE(old_tex);
        }
#undef TEXTURE
    }
}

#define MIN_BATCH globals4es.minbatch
#define MAX_BATCH globals4es.maxbatch

static int ensure_gpu_quad_ibo_u16(size_t needed_quads) {
    if (needed_quads > 16384) return 0; /* 16384 * 4 == 65536 max 16-bit vertex index */
    if (glstate->quad_ibo_u16_quads >= needed_quads &&
        glstate->quad_ibo_u16.real_buffer && glstate->quad_ibo_u16.data) {
        return 1;
    }
    /* Allocate full 16,384-quad (196 KB) GPU IBO once per context so chunk draws never reallocate */
    size_t cap_quads = 16384;
    size_t byte_size = cap_quads * 6 * sizeof(GLushort);
    GLushort* data = (GLushort*)malloc(byte_size);
    if (!data) return 0;
    for (size_t q = 0; q < cap_quads; ++q) {
        GLushort v = (GLushort)(q * 4);
        size_t o = q * 6;
        data[o + 0] = v + 0;
        data[o + 1] = v + 1;
        data[o + 2] = v + 2;
        data[o + 3] = v + 0;
        data[o + 4] = v + 2;
        data[o + 5] = v + 3;
    }

    LOAD_GLES(glGenBuffers);
    LOAD_GLES(glBufferData);
    if (!gles_glGenBuffers || !gles_glBufferData) {
        free(data);
        return 0;
    }

    if (!glstate->quad_ibo_u16.real_buffer) {
        gles_glGenBuffers(1, &glstate->quad_ibo_u16.real_buffer);
    }
    if (!glstate->quad_ibo_u16.real_buffer) {
        free(data);
        return 0;
    }

    free(glstate->quad_ibo_u16.data);
    glstate->quad_ibo_u16.data = data;
    glstate->quad_ibo_u16.size = (GLsizeiptr)byte_size;
    glstate->quad_ibo_u16.type = GL_ELEMENT_ARRAY_BUFFER;
    glstate->quad_ibo_u16.usage = GL_STATIC_DRAW;
    glstate->quad_ibo_u16_quads = cap_quads;

    GLuint prev_want = glstate->bind_buffer.want_index;
    GLuint prev_idx = glstate->bind_buffer.index;
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, glstate->quad_ibo_u16.real_buffer);
    gles_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)byte_size, data, GL_STATIC_DRAW);
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_idx);
    glstate->bind_buffer.want_index = prev_want;
    return 1;
}

/* Lazy-growing persistent 32-bit quad IBO capped at 262,144 quads (1,048,576 vertices = 6 MB max)
   for ES 3.0/3.1 GPUs without glDrawElementsBaseVertex. On ES 3.2+ (hardext.basevertex),
   quad_ibo_u16 (196 KB) handles arbitrary arena offsets without ever allocating quad_ibo_u32. */
#define EMESIS_MAX_QUAD_IBO_U32_QUADS 262144

static int ensure_gpu_quad_ibo_u32(size_t needed_quads) {
    if (!hardext.elementuint || needed_quads > EMESIS_MAX_QUAD_IBO_U32_QUADS) return 0;
    if (glstate->quad_ibo_u32_quads >= needed_quads &&
        glstate->quad_ibo_u32.real_buffer && glstate->quad_ibo_u32.data) {
        return 1;
    }
    size_t cap_quads = 32768;
    while (cap_quads < needed_quads && cap_quads < EMESIS_MAX_QUAD_IBO_U32_QUADS) {
        cap_quads <<= 1;
    }
    if (cap_quads > EMESIS_MAX_QUAD_IBO_U32_QUADS) cap_quads = EMESIS_MAX_QUAD_IBO_U32_QUADS;
    size_t byte_size = cap_quads * 6 * sizeof(GLuint);
    GLuint* data = (GLuint*)malloc(byte_size);
    if (!data) return 0;
    for (size_t q = 0; q < cap_quads; ++q) {
        GLuint v = (GLuint)(q * 4);
        size_t o = q * 6;
        data[o + 0] = v + 0;
        data[o + 1] = v + 1;
        data[o + 2] = v + 2;
        data[o + 3] = v + 0;
        data[o + 4] = v + 2;
        data[o + 5] = v + 3;
    }

    LOAD_GLES(glGenBuffers);
    LOAD_GLES(glBufferData);
    if (!gles_glGenBuffers || !gles_glBufferData) {
        free(data);
        return 0;
    }

    if (!glstate->quad_ibo_u32.real_buffer) {
        gles_glGenBuffers(1, &glstate->quad_ibo_u32.real_buffer);
    }
    if (!glstate->quad_ibo_u32.real_buffer) {
        free(data);
        return 0;
    }

    free(glstate->quad_ibo_u32.data);
    glstate->quad_ibo_u32.data = data;
    glstate->quad_ibo_u32.size = (GLsizeiptr)byte_size;
    glstate->quad_ibo_u32.type = GL_ELEMENT_ARRAY_BUFFER;
    glstate->quad_ibo_u32.usage = GL_STATIC_DRAW;
    glstate->quad_ibo_u32_quads = cap_quads;

    GLuint prev_want = glstate->bind_buffer.want_index;
    GLuint prev_idx = glstate->bind_buffer.index;
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, glstate->quad_ibo_u32.real_buffer);
    gles_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)byte_size, data, GL_STATIC_DRAW);
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_idx);
    glstate->bind_buffer.want_index = prev_want;
    return 1;
}

static int emesis_vao_has_only_vbo_attribs(int allow_instanced_divisor) {
    if (!glstate || !glstate->vao || hardext.esversion <= 1) return 0;
    int any_en = 0;
    for (int a = 0; a < hardext.maxvattrib; ++a) {
        vertexattrib_t* w = &glstate->vao->vertexattrib[a];
        if (!w->enabled) continue;
        any_en = 1;
        if (w->divisor != 0) {
            if (!allow_instanced_divisor) return 0;
            if (!w->buffer && !w->pointer) return 0;
            continue;
        }
        if (w->buffer && w->size != GL_BGRA && w->type != GL_DOUBLE &&
            (!w->real_buffer || w->buffer->gpu_dirty)) {
            emesis_sync_gpu_buffer(w->buffer, GL_ARRAY_BUFFER);
            w->real_buffer = w->buffer->real_buffer;
        }
        if (!w->real_buffer || w->size == GL_BGRA || w->type == GL_DOUBLE) {
            return 0;
        }
    }
    return any_en;
}

static void draw_quads_arrays_fast(GLint first, GLsizei count, int instancecount) {
    GLsizei nquads = count / 4;
    if (nquads <= 0 || first < 0) return;
    GLsizei idx_count = nquads * 6;
    GLsizei used_verts = nquads * 4;
    size_t max_vert = (size_t)first + (size_t)used_verts;
    int only_vbo = (hardext.esversion > 1 && glstate->render_mode != GL_SELECT)
                       ? emesis_vao_has_only_vbo_attribs(1)
                       : 0;

    /* 1. Pure GPU VBO fast paths: keep GPU quad_ibo bound lazily across consecutive draws (zero IBO ping-pong) */
    if (only_vbo) {
        if (instancecount == 1 && hardext.basevertex &&
            ensure_gpu_quad_ibo_u16((nquads <= 16384) ? (size_t)nquads : 16384)) {
            LOAD_GLES3_OR_EXT(glDrawElementsBaseVertex);
            if (gles_glDrawElementsBaseVertex) {
                if (glstate->raster.bm_drawing) bitmap_flush();
                GLuint prev_want = glstate->bind_buffer.want_index;
                realize_textures(1);
                scratch_t scratch = {0};
                realize_glenv(0, first, used_verts, 0, NULL, &scratch);
                bindBuffer(GL_ELEMENT_ARRAY_BUFFER, glstate->quad_ibo_u16.real_buffer);

                GLsizei rem_q = nquads;
                GLint cur_first = first;
                while (rem_q > 0) {
                    GLsizei cq = (rem_q > 16384) ? 16384 : rem_q;
                    gles_glDrawElementsBaseVertex(GL_TRIANGLES, cq * 6, GL_UNSIGNED_SHORT, NULL, cur_first);
                    cur_first += cq * 4;
                    rem_q -= cq;
                }

                free_scratch(&scratch);
                glstate->bind_buffer.want_index = prev_want;
                return;
            }
        }

        if ((first & 3) == 0) {
            size_t q0 = (size_t)first >> 2;
            size_t needed_quads = q0 + (size_t)nquads;
            int use_u32 = (max_vert > 65536);
            if ((!use_u32 && ensure_gpu_quad_ibo_u16(needed_quads)) ||
                (use_u32 && ensure_gpu_quad_ibo_u32(needed_quads))) {
                glbuffer_t* qibo = use_u32 ? &glstate->quad_ibo_u32 : &glstate->quad_ibo_u16;
                size_t idx_bytes = use_u32 ? sizeof(GLuint) : sizeof(GLushort);
                GLenum idx_type = use_u32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
                const void* byte_off = (const void*)(q0 * 6u * idx_bytes);

                if (instancecount == 1) {
                    if (glstate->raster.bm_drawing) bitmap_flush();
                    LOAD_GLES2(glDrawElements);
                    GLuint prev_want = glstate->bind_buffer.want_index;
                    realize_textures(1);
                    scratch_t scratch = {0};
                    realize_glenv(0, first, used_verts, 0, NULL, &scratch);
                    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, qibo->real_buffer);
                    gles_glDrawElements(GL_TRIANGLES, idx_count, idx_type, byte_off);
                    free_scratch(&scratch);
                    glstate->bind_buffer.want_index = prev_want;
                    return;
                } else {
                    glbuffer_t* prev_elem = glstate->vao->elements;
                    GLuint prev_want = glstate->bind_buffer.want_index;
                    glstate->vao->elements = qibo;
                    if (use_u32) {
                        const GLuint* iptr = (const GLuint*)qibo->data + (q0 * 6u);
                        glDrawElementsCommon(GL_TRIANGLES, 0, idx_count, (GLuint)max_vert, NULL, iptr, instancecount);
                    } else {
                        const GLushort* sptr = (const GLushort*)qibo->data + (q0 * 6u);
                        glDrawElementsCommon(GL_TRIANGLES, 0, idx_count, (GLuint)max_vert, sptr, NULL, instancecount);
                    }
                    glstate->vao->elements = prev_elem;
                    glstate->bind_buffer.want_index = prev_want;
                    return;
                }
            }
        }
    }

    /* 2. Client-pointer / GL_BGRA fast path (Tessellator / BufferUploader / HUD / entities):
     * NEVER bind GPU GL_ELEMENT_ARRAY_BUFFER while vertex attribs are client-RAM pointers (avoids Mali GPU IBO
     * readback/scan stalls); pass precomputed static CPU quad_ibo.data with ELEMENT_ARRAY_BUFFER=0 and O(1) range. */
    if (!only_vbo && hardext.esversion > 1 && glstate->render_mode != GL_SELECT && instancecount == 1 &&
        ((first & 3) == 0)) {
        size_t q0 = (size_t)first >> 2;
        size_t needed_quads = q0 + (size_t)nquads;
        int use_u32 = (max_vert > 65536);
        if ((!use_u32 && ensure_gpu_quad_ibo_u16(needed_quads)) ||
            (use_u32 && ensure_gpu_quad_ibo_u32(needed_quads))) {
            if (glstate->raster.bm_drawing) bitmap_flush();
            LOAD_GLES2(glDrawElements);
            GLuint old_want = wantBufferIndex(0);
            realize_textures(1);
            scratch_t scratch = {0};
            realize_glenv(0, first, used_verts, 0, NULL, &scratch);
            realize_bufferIndex();
            if (use_u32) {
                const GLuint* iptr = (const GLuint*)glstate->quad_ibo_u32.data + (q0 * 6u);
                gles_glDrawElements(GL_TRIANGLES, idx_count, GL_UNSIGNED_INT, iptr);
            } else {
                const GLushort* sptr = (const GLushort*)glstate->quad_ibo_u16.data + (q0 * 6u);
                gles_glDrawElements(GL_TRIANGLES, idx_count, GL_UNSIGNED_SHORT, sptr);
            }
            free_scratch(&scratch);
            wantBufferIndex(old_want);
            return;
        }
    }

    /* 3. Unaligned `first` / ES 1.1 / GL_SELECT fallback */
    glbuffer_t* prev_elem = glstate->vao->elements;
    GLuint old_buffer = wantBufferIndex(0);
    glstate->vao->elements = NULL;
    if (max_vert > 65536 && hardext.elementuint) {
        gl4es_scratch((size_t)idx_count * sizeof(GLuint));
        GLuint* p = (GLuint*)glstate->scratch;
        for (GLsizei q = 0; q < nquads; ++q) {
            GLuint v = (GLuint)first + (GLuint)(q * 4);
            p[0] = v + 0; p[1] = v + 1; p[2] = v + 2;
            p[3] = v + 0; p[4] = v + 2; p[5] = v + 3;
            p += 6;
        }
        glDrawElementsCommon(GL_TRIANGLES, 0, idx_count, (GLuint)max_vert, NULL, (const GLuint*)glstate->scratch,
                             instancecount);
    } else {
        gl4es_scratch((size_t)idx_count * sizeof(GLushort));
        GLushort* p = (GLushort*)glstate->scratch;
        for (GLsizei q = 0; q < nquads; ++q) {
            GLushort v = (GLushort)(first + q * 4);
            p[0] = v + 0; p[1] = v + 1; p[2] = v + 2;
            p[3] = v + 0; p[4] = v + 2; p[5] = v + 3;
            p += 6;
        }
        glDrawElementsCommon(GL_TRIANGLES, 0, idx_count, (GLuint)max_vert, (const GLushort*)glstate->scratch, NULL,
                             instancecount);
    }
    glstate->vao->elements = prev_elem;
    wantBufferIndex(old_buffer);
}

void APIENTRY_GL4ES gl4es_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                              const void* indices) {
    DBG(SHUT_LOGD("glDrawRangeElements(%s, %i, %i, %i, %s, @%p), inlist=%i, pending=%d\n", PrintEnum(mode), start, end,
                  count, PrintEnum(type), indices, (glstate->list.active) ? 1 : 0, glstate->list.pending);)
    count = adjust_vertices(mode, count);

    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }

    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);

    // BATCH Mode
    if (!compiling) {
        if ((!intercept && !glstate->list.pending && (count >= MIN_BATCH && count <= MAX_BATCH)) ||
            (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

    noerrorShim();
    GLushort* sindices = NULL;
    GLuint* iindices = NULL;
    bool need_free =
        !((type == GL_UNSIGNED_SHORT) || (!compiling && !intercept && type == GL_UNSIGNED_INT && hardext.elementuint));
    if (need_free) {
        sindices = copy_gl_array(
            (glstate->vao->elements) ? (void*)((char*)glstate->vao->elements->data + (uintptr_t)indices) : indices,
            type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);
    } else {
        if (type == GL_UNSIGNED_INT)
            iindices = (glstate->vao->elements) ? ((void*)((char*)glstate->vao->elements->data + (uintptr_t)indices))
                                                : (GLvoid*)indices;
        else
            sindices = (glstate->vao->elements) ? ((void*)((char*)glstate->vao->elements->data + (uintptr_t)indices))
                                                : (GLvoid*)indices;
    }

    if (compiling) {
        // TODO, handle uint indices
        renderlist_t* list = glstate->list.active;

        if (!need_free) {
            GLushort* tmp = sindices;
            sindices = (GLushort*)malloc(count * sizeof(GLushort));
            memcpy(sindices, tmp, count * sizeof(GLushort));
        }
        for (int i = 0; i < count; i++)
            sindices[i] -= start; // TODO: should be optimizable

        if (globals4es.mergelist && list->stage >= STAGE_DRAW && is_list_compatible(list) && !list->use_glstate &&
            sindices) {
            list = NewDrawStage(list, mode);
            if (list->vert) {
                glstate->list.active = arrays_add_renderlist(list, mode, start, end + 1, sindices, count);
                NewStage(glstate->list.active, STAGE_POSTDRAW);
                return;
            }
        }

        NewStage(list, STAGE_DRAW);

        glstate->list.active = list = arrays_to_renderlist(list, mode, start, end + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        // end_renderlist(list);

        NewStage(glstate->list.active, STAGE_POSTDRAW);
        return;
    }

    if (intercept) {
        // TODO handling uint indices
        renderlist_t* list = NULL;

        if (!need_free) {
            GLushort* tmp = sindices;
            sindices = (GLushort*)malloc(count * sizeof(GLushort));
            memcpy(sindices, tmp, count * sizeof(GLushort));
        }
        for (int i = 0; i < count; i++)
            sindices[i] -= start;
        list = arrays_to_renderlist(list, mode, start, end + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);

        return;
    } else {
        glDrawElementsCommon(mode, 0, count, end + 1, sindices, iindices, 1);
        if (need_free) free(sindices);
    }
}
AliasExport(void, glDrawRangeElements, ,
            (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices));
AliasExport(void, glDrawRangeElements, EXT,
            (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices));

void APIENTRY_GL4ES gl4es_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices) {
    DBG(SHUT_LOGD("glDrawElements(%s, %d, %s, %p), vtx=%p map=%p, pending=%d\n", PrintEnum(mode), count,
                  PrintEnum(type), indices, (glstate->vao->vertex) ? glstate->vao->vertex->data : NULL,
                  (glstate->vao->elements) ? glstate->vao->elements->data : NULL, glstate->list.pending);)
    // TODO: split for count > 65535?
    // special check for QUADS and TRIANGLES that need multiple of 4 or 3 vertex...
    count = adjust_vertices(mode, count);

    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }

    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);

    // BATCH Mode
    if (!compiling) {
        if ((!intercept && !glstate->list.pending && (count >= MIN_BATCH && count <= MAX_BATCH)) ||
            (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

    noerrorShim();
    GLushort* sindices = NULL;
    GLuint* iindices = NULL;
    GLuint old_index = 0;
    bool need_free =
        !((type == GL_UNSIGNED_SHORT) || (!compiling && !intercept && type == GL_UNSIGNED_INT && hardext.elementuint));
    if (need_free) {
        sindices = copy_gl_array(
            (glstate->vao->elements) ? (void*)((char*)glstate->vao->elements->data + (uintptr_t)indices) : indices,
            type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);
        old_index = wantBufferIndex(0);
    } else {
        if (type == GL_UNSIGNED_INT)
            iindices = (glstate->vao->elements) ? ((void*)((char*)glstate->vao->elements->data + (uintptr_t)indices))
                                                : (GLvoid*)indices;
        else
            sindices = (glstate->vao->elements) ? ((void*)((char*)glstate->vao->elements->data + (uintptr_t)indices))
                                                : (GLvoid*)indices;
    }

    if (compiling) {
        // TODO, handle uint indices
        renderlist_t* list = glstate->list.active;
        GLsizei min, max;

        if (!need_free) {
            GLushort* tmp = sindices;
            sindices = (GLushort*)malloc(count * sizeof(GLushort));
            memcpy(sindices, tmp, count * sizeof(GLushort));
        }

        normalize_indices_us(sindices, &max, &min, count);

        if (globals4es.mergelist && list->stage >= STAGE_DRAW && is_list_compatible(list) && !list->use_glstate &&
            sindices) {
            list = NewDrawStage(list, mode);
            glstate->list.active = arrays_add_renderlist(list, mode, min, max + 1, sindices, count);
            NewStage(glstate->list.active, STAGE_POSTDRAW);
            return;
        }

        NewStage(list, STAGE_DRAW);

        glstate->list.active = list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        // end_renderlist(list);

        NewStage(glstate->list.active, STAGE_POSTDRAW);
        return;
    }

    if (intercept) {
        // TODO handling uint indices
        renderlist_t* list = NULL;
        GLsizei min, max;

        if (!need_free) {
            GLushort* tmp = sindices;
            sindices = (GLushort*)malloc(count * sizeof(GLushort));
            memcpy(sindices, tmp, count * sizeof(GLushort));
        }
        normalize_indices_us(sindices, &max, &min, count);
        list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
        return;
    } else {
        glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, 1);
        if (need_free) {
            free(sindices);
            wantBufferIndex(old_index);
        }
    }
}
AliasExport(void, glDrawElements, , (GLenum mode, GLsizei count, GLenum type, const GLvoid* indices));

void APIENTRY_GL4ES gl4es_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    DBG(SHUT_LOGD("glDrawArrays(%s, %d, %d), list=%p pending=%d\n", PrintEnum(mode), first, count, glstate->list.active,
                  glstate->list.pending);)
    // special check for QUADS and TRIANGLES that need multiple of 4 or 3 vertex...
    count = adjust_vertices(mode, count);

    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }

    // special case for (very) large GL_QUADS array on ES 1.1 only (ES 2/3 handles arbitrary sizes in draw_quads_arrays_fast)
    if (hardext.esversion == 1 && (mode == GL_QUADS) && (count > 4 * 8000)) {
        // split the array in manageable slice
        int cnt = 4 * 8000;
        for (int i = 0; i < count; i += 4 * 8000) {
            if (i + cnt > count) cnt = count - i;
            gl4es_glDrawArrays(mode, first + i, cnt);
        }
        return;
    }
    noerrorShim();

    bool intercept = should_intercept_render(mode);
    // BATCH Mode
    if (!glstate->list.compiling) {
        if ((!intercept && !glstate->list.pending && (count >= MIN_BATCH && count <= MAX_BATCH)) ||
            (intercept && globals4es.maxbatch)) {
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

    if (glstate->list.active) {
        renderlist_t* list = glstate->list.active;

        if (globals4es.mergelist && list->stage >= STAGE_DRAW && is_list_compatible(list) && !list->use_glstate) {
            list = NewDrawStage(list, mode);
            if (list->vert) {
                glstate->list.active = arrays_add_renderlist(list, mode, first, count + first, NULL, 0);
                NewStage(glstate->list.active, STAGE_POSTDRAW);
                return;
            }
        }

        NewStage(list, STAGE_DRAW);
        glstate->list.active = arrays_to_renderlist(list, mode, first, count + first);
        NewStage(glstate->list.active, STAGE_POSTDRAW);
        return;
    }

    /*if (glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
        mode = GL_LINE_LOOP;*/
    if (glstate->polygon_mode == GL_POINT && mode >= GL_TRIANGLES) mode = GL_POINTS;

    if (intercept) {
        renderlist_t* list;
        list = arrays_to_renderlist(NULL, mode, first, count + first);
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    } else {
        if (mode == GL_QUADS) {
            draw_quads_arrays_fast(first, count, 1);
            return;
        }

        glDrawElementsCommon(mode, first, count, count, NULL, NULL, 1);
    }
}
AliasExport(void, glDrawArrays, , (GLenum mode, GLint first, GLsizei count));
AliasExport(void, glDrawArrays, EXT, (GLenum mode, GLint first, GLsizei count));

void APIENTRY_GL4ES gl4es_glMultiDrawArrays(GLenum mode, const GLint* firsts, const GLsizei* counts,
                                            GLsizei primcount) {
    DBG(SHUT_LOGD("glMultiDrawArrays(%s, %p, %p, %d), list=%p pending=%d\n", PrintEnum(mode), firsts, counts, primcount,
                  glstate->list.active, glstate->list.pending);)
    if (primcount <= 0) {
        if (primcount < 0) errorShim(GL_INVALID_VALUE);
        else noerrorShim();
        return;
    }
    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);

    /* Fast GPU path for VBO-backed GL_QUADS (OptiFine RenderRegions / Sodium < 1.17) and ES native modes */
    if (!compiling && !intercept && glstate->polygon_mode != GL_LINE) {
        if (glstate->list.pending) gl4es_flush();
        GLenum draw_mode = (glstate->polygon_mode == GL_POINT && mode >= GL_TRIANGLES) ? GL_POINTS : mode;
        int all_vbo_attribs = emesis_vao_has_only_vbo_attribs(1);

        if (draw_mode == GL_QUADS && hardext.esversion > 1 && glstate->render_mode != GL_SELECT) {
            /* Coalesce contiguous (first, count) quad slices in-place (Sodium ChunkRenderBackendOneshot packs
             * visible faces sequentially inside each chunk VBO, merging 2..7 slices into 1..3 runs) */
            GLint stack_rfirst[256];
            GLsizei stack_rnquads[256];
            GLint* rfirst = (primcount <= 256) ? stack_rfirst : (GLint*)malloc((size_t)primcount * sizeof(GLint));
            GLsizei* rnquads =
                (primcount <= 256) ? stack_rnquads : (GLsizei*)malloc((size_t)primcount * sizeof(GLsizei));
            if (rfirst && rnquads) {
                int n_runs = 0;
                size_t max_vert = 0;
                int all_aligned = 1;
                for (int i = 0; i < primcount; ++i) {
                    if (counts[i] < 0) {
                        if (rfirst != stack_rfirst) free(rfirst);
                        if (rnquads != stack_rnquads) free(rnquads);
                        errorShim(GL_INVALID_VALUE);
                        return;
                    }
                    GLsizei q = counts[i] / 4;
                    if (q <= 0 || firsts[i] < 0) continue;
                    GLint f = firsts[i];
                    size_t end_v = (size_t)f + (size_t)q * 4;
                    if (end_v > max_vert) max_vert = end_v;
                    if (n_runs > 0 && (size_t)rfirst[n_runs - 1] + (size_t)rnquads[n_runs - 1] * 4 == (size_t)f) {
                        rnquads[n_runs - 1] += q;
                    } else {
                        if (f & 3) all_aligned = 0;
                        rfirst[n_runs] = f;
                        rnquads[n_runs] = q;
                        ++n_runs;
                    }
                }
                if (n_runs == 0) {
                    if (rfirst != stack_rfirst) free(rfirst);
                    if (rnquads != stack_rnquads) free(rnquads);
                    noerrorShim();
                    return;
                }

                /* ES 3.2+ basevertex fast path: dispatches all coalesced quad runs via 16-bit quad_ibo_u16
                 * regardless of vertex alignment or arena offset (>65,536 vertices). */
                if (all_vbo_attribs && hardext.basevertex && ensure_gpu_quad_ibo_u16(16384)) {
                    LOAD_GLES3_OR_EXT(glDrawElementsBaseVertex);
                    if (gles_glDrawElementsBaseVertex) {
                        if (glstate->raster.bm_drawing) bitmap_flush();
                        glbuffer_t* prev_elem = glstate->vao->elements;
                        GLuint prev_want = glstate->bind_buffer.want_index;
                        glstate->vao->elements = &glstate->quad_ibo_u16;

                        realize_textures(1);
                        scratch_t scratch = {0};
                        realize_glenv(0, 0, (int)max_vert, GL_UNSIGNED_SHORT, NULL, &scratch);
                        bindBuffer(GL_ELEMENT_ARRAY_BUFFER, glstate->quad_ibo_u16.real_buffer);

                        for (int r = 0; r < n_runs; ++r) {
                            GLsizei rem_q = rnquads[r];
                            GLint cur_first = rfirst[r];
                            while (rem_q > 0) {
                                GLsizei cq = (rem_q > 16384) ? 16384 : rem_q;
                                gles_glDrawElementsBaseVertex(GL_TRIANGLES, cq * 6, GL_UNSIGNED_SHORT, NULL, cur_first);
                                cur_first += cq * 4;
                                rem_q -= cq;
                            }
                        }

                        free_scratch(&scratch);
                        glstate->vao->elements = prev_elem;
                        glstate->bind_buffer.want_index = prev_want;
                        if (rfirst != stack_rfirst) free(rfirst);
                        if (rnquads != stack_rnquads) free(rnquads);
                        errorGL();
                        return;
                    }
                }

                /* Universal zero-upload GPU quad_ibo path: works on BOTH hardext.multidraw == 1 (Adreno)
                 * AND hardext.multidraw == 0 (ARM Mali), keeping quad_ibo persistently bound in VRAM
                 * with a single realize_glenv + realize_textures pass and zero CPU index generation. */
                if (all_vbo_attribs && all_aligned &&
                    ((max_vert <= 65536 && ensure_gpu_quad_ibo_u16(max_vert >> 2)) ||
                     (max_vert > 65536 && ensure_gpu_quad_ibo_u32(max_vert >> 2)))) {
                    if (glstate->raster.bm_drawing) bitmap_flush();
                    int use_u32 = (max_vert > 65536);
                    glbuffer_t* qibo = use_u32 ? &glstate->quad_ibo_u32 : &glstate->quad_ibo_u16;
                    size_t idx_bytes = use_u32 ? sizeof(GLuint) : sizeof(GLushort);
                    GLenum idx_type = use_u32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;

                    glbuffer_t* prev_elem = glstate->vao->elements;
                    GLuint prev_want = glstate->bind_buffer.want_index;
                    glstate->vao->elements = qibo;

                    realize_textures(1);
                    scratch_t scratch = {0};
                    realize_glenv(0, 0, (int)max_vert, 0, NULL, &scratch);
                    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, qibo->real_buffer);

                    int drew_multi = 0;
                    if (n_runs >= 2 && hardext.multidraw) {
                        LOAD_GLES_EXT(glMultiDrawElements);
                        if (gles_glMultiDrawElements &&
                            (void*)gles_glMultiDrawElements != (void*)gl4es_glMultiDrawElements) {
                            GLsizei stack_cnts[256];
                            const void* stack_offs[256];
                            GLsizei* sub_cnts = (n_runs <= 256)
                                                    ? stack_cnts
                                                    : (GLsizei*)malloc((size_t)n_runs * sizeof(GLsizei));
                            const void** sub_offs = (n_runs <= 256)
                                                        ? stack_offs
                                                        : (const void**)malloc((size_t)n_runs * sizeof(void*));
                            if (sub_cnts && sub_offs) {
                                for (int r = 0; r < n_runs; ++r) {
                                    sub_cnts[r] = rnquads[r] * 6;
                                    sub_offs[r] = (const void*)(((size_t)rfirst[r] >> 2) * 6 * idx_bytes);
                                }
                                gles_glMultiDrawElements(GL_TRIANGLES, sub_cnts, idx_type, sub_offs, n_runs);
                                drew_multi = 1;
                            }
                            if (sub_cnts && sub_cnts != stack_cnts) free(sub_cnts);
                            if (sub_offs && sub_offs != stack_offs) free(sub_offs);
                        }
                    }
                    if (!drew_multi) {
                        LOAD_GLES2(glDrawElements);
                        for (int r = 0; r < n_runs; ++r) {
                            gles_glDrawElements(GL_TRIANGLES, rnquads[r] * 6, idx_type,
                                                (const void*)(((size_t)rfirst[r] >> 2) * 6 * idx_bytes));
                        }
                    }

                    free_scratch(&scratch);
                    glstate->vao->elements = prev_elem;
                    glstate->bind_buffer.want_index = prev_want;
                    if (rfirst != stack_rfirst) free(rfirst);
                    if (rnquads != stack_rnquads) free(rnquads);
                    errorGL();
                    return;
                }

                /* Fallback for client-pointer/BGRA/unaligned/huge (>256k vert) quad draws:
                 * never unbind GL_ELEMENT_ARRAY_BUFFER across VBO chunks */
                for (int r = 0; r < n_runs; ++r) {
                    draw_quads_arrays_fast(rfirst[r], rnquads[r] * 4, 1);
                }
                if (rfirst != stack_rfirst) free(rfirst);
                if (rnquads != stack_rnquads) free(rnquads);
                errorGL();
                return;
            }
            if (rfirst && rfirst != stack_rfirst) free(rfirst);
            if (rnquads && rnquads != stack_rnquads) free(rnquads);
        }

        if (draw_mode != GL_QUADS && draw_mode != GL_QUAD_STRIP && draw_mode != GL_POLYGON && hardext.esversion > 1 &&
            all_vbo_attribs && glstate->render_mode != GL_SELECT) {
            int can_merge = (draw_mode == GL_TRIANGLES || draw_mode == GL_LINES || draw_mode == GL_POINTS);
            GLint stack_rf[256];
            GLsizei stack_rc[256];
            GLint* rf = (primcount <= 256) ? stack_rf : (GLint*)malloc((size_t)primcount * sizeof(GLint));
            GLsizei* rc = (primcount <= 256) ? stack_rc : (GLsizei*)malloc((size_t)primcount * sizeof(GLsizei));
            if (rf && rc) {
                int n_runs = 0;
                GLsizei max_end = 0;
                for (int i = 0; i < primcount; ++i) {
                    if (counts[i] < 0) {
                        if (rf != stack_rf) free(rf);
                        if (rc != stack_rc) free(rc);
                        errorShim(GL_INVALID_VALUE);
                        return;
                    }
                    GLsizei c = adjust_vertices(draw_mode, counts[i]);
                    if (c <= 0 || firsts[i] < 0) continue;
                    GLint f = firsts[i];
                    if (f + c > max_end) max_end = f + c;
                    if (can_merge && n_runs > 0 && rf[n_runs - 1] + rc[n_runs - 1] == f) {
                        rc[n_runs - 1] += c;
                    } else {
                        rf[n_runs] = f;
                        rc[n_runs] = c;
                        ++n_runs;
                    }
                }
                if (n_runs == 0) {
                    if (rf != stack_rf) free(rf);
                    if (rc != stack_rc) free(rc);
                    noerrorShim();
                    return;
                }
                if (glstate->raster.bm_drawing) bitmap_flush();
                realize_textures(1);
                scratch_t scratch = {0};
                realize_glenv(draw_mode == GL_POINTS, 0, max_end, 0, NULL, &scratch);
                int drew_multi = 0;
                if (n_runs >= 2 && hardext.multidraw) {
                    LOAD_GLES_EXT(glMultiDrawArrays);
                    if (gles_glMultiDrawArrays &&
                        (void*)gles_glMultiDrawArrays != (void*)gl4es_glMultiDrawArrays) {
                        gles_glMultiDrawArrays(draw_mode, rf, rc, n_runs);
                        drew_multi = 1;
                    }
                }
                if (!drew_multi) {
                    LOAD_GLES(glDrawArrays);
                    for (int r = 0; r < n_runs; ++r) {
                        gles_glDrawArrays(draw_mode, rf[r], rc[r]);
                    }
                }
                free_scratch(&scratch);
                if (rf != stack_rf) free(rf);
                if (rc != stack_rc) free(rc);
                errorGL();
                return;
            }
            if (rf && rf != stack_rf) free(rf);
            if (rc && rc != stack_rc) free(rc);
        }
    }

    GLsizei maxcount = counts[0];
    GLsizei mincount = counts[0];
    for (int i = 1; i < primcount; i++) {
        if (counts[i] > maxcount) maxcount = counts[i];
        if (counts[i] < mincount) mincount = counts[i];
    }
    // BATCH Mode
    if (!compiling) {
        if (!intercept && glstate->list.pending &&
            maxcount > MAX_BATCH) // too large and will not intercept, stop the BATCH
            gl4es_flush();
        else if ((!intercept && !glstate->list.pending && mincount < MIN_BATCH) || (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }
    renderlist_t* list = NULL;

    GLenum err = 0;

    for (int i = 0; i < primcount; i++) {
        GLsizei count = adjust_vertices(mode, counts[i]);
        GLint first = firsts[i];

        if (count < 0) {
            err = GL_INVALID_VALUE;
            continue;
        }
        if (count == 0) {
            continue;
        }

        if (compiling) {
            if (globals4es.mergelist && glstate->list.active->stage >= STAGE_DRAW &&
                is_list_compatible(glstate->list.active) && !glstate->list.active->use_glstate) {
                glstate->list.active = NewDrawStage(glstate->list.active, mode);
                glstate->list.active = arrays_add_renderlist(glstate->list.active, mode, first, count + first, NULL, 0);
                NewStage(glstate->list.active, STAGE_POSTDRAW);
                continue;
            }

            NewStage(glstate->list.active, STAGE_DRAW);
            glstate->list.active = arrays_to_renderlist(glstate->list.active, mode, first, count + first);
            NewStage(glstate->list.active, STAGE_POSTDRAW);

            continue;
        }

        if (glstate->polygon_mode == GL_POINT && mode >= GL_TRIANGLES) mode = GL_POINTS;

        if (intercept) {
            if (list) {
                NewStage(list, STAGE_DRAW);
            }
            if (globals4es.mergelist && list->stage >= STAGE_DRAW && is_list_compatible(list) && !list->use_glstate) {
                list = NewDrawStage(list, mode);
                list = arrays_add_renderlist(list, mode, first, count + first, NULL, 0);
                NewStage(list, STAGE_POSTDRAW);
            } else
                list = arrays_to_renderlist(NULL, mode, first, count + first);
        } else {
            if (mode == GL_QUADS) {
                draw_quads_arrays_fast(first, count, 1);
                continue;
            }

            glDrawElementsCommon(mode, first, count, count, NULL, NULL, 1);
        }
    }
    if (list) {
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    }
    if (err)
        errorShim(err);
    else
        errorGL();
}
AliasExport(void, glMultiDrawArrays, , (GLenum mode, const GLint* first, const GLsizei* count, GLsizei primcount));
AliasExport(void, glMultiDrawArrays, EXT, (GLenum mode, const GLint* first, const GLsizei* count, GLsizei primcount));

void APIENTRY_GL4ES gl4es_glMultiDrawElements(GLenum mode, GLsizei* counts, GLenum type, const void* const* indices,
                                              GLsizei primcount) {
    DBG(SHUT_LOGD("glMultiDrawElements(%s, %p, %s, @%p, %d), inlist=%i, pending=%d\n", PrintEnum(mode), counts,
                  PrintEnum(type), indices, primcount, (glstate->list.active) ? 1 : 0, glstate->list.pending);)
    if (primcount <= 0) {
        if (primcount < 0) errorShim(GL_INVALID_VALUE);
        else noerrorShim();
        return;
    }
    if (glstate->list.active || should_intercept_render(mode) || mode == GL_QUADS || mode == GL_QUAD_STRIP ||
        mode == GL_POLYGON || glstate->polygon_mode == GL_LINE || type == GL_UNSIGNED_BYTE) {
        for (int i = 0; i < primcount; i++) {
            if (counts[i] > 0) gl4es_glDrawElements(mode, counts[i], type, indices[i]);
        }
        return;
    }
    if (glstate->list.pending) gl4es_flush();

    GLenum draw_mode = (glstate->polygon_mode == GL_POINT && mode >= GL_TRIANGLES) ? GL_POINTS : mode;
    GLuint elem_buf = (glstate->vao && glstate->vao->elements) ? glstate->vao->elements->real_buffer : 0;
    const void* first_cpu_idx = NULL;
    if (indices) {
        if (elem_buf && glstate->vao->elements->data) {
            first_cpu_idx = (const void*)((const char*)glstate->vao->elements->data + (uintptr_t)indices[0]);
        } else if (!elem_buf) {
            first_cpu_idx = indices[0];
        }
    }

    scratch_t scratch = {0};
    realize_glenv(draw_mode == GL_POINTS, 0, counts ? counts[0] : 0, first_cpu_idx ? type : 0, first_cpu_idx, &scratch);

    realize_textures(1);
    GLuint prev_want = glstate->bind_buffer.want_index;
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, elem_buf);

    if (hardext.multidraw && elem_buf) {
        LOAD_GLES_EXT(glMultiDrawElements);
        if (gles_glMultiDrawElements &&
            (void*)gles_glMultiDrawElements != (void*)gl4es_glMultiDrawElements) {
            gles_glMultiDrawElements(draw_mode, counts, type, indices, primcount);
            glstate->bind_buffer.want_index = prev_want;
            free_scratch(&scratch);
            errorGL();
            return;
        }
    }

    LOAD_GLES2(glDrawElements);
    DBG(SHUT_LOGD("gles_glDrawElements loop x%d\n", primcount);)
    for (int i = 0; i < primcount; i++) {
        if (counts[i] > 0) gles_glDrawElements(draw_mode, counts[i], type, indices[i]);
    }
    glstate->bind_buffer.want_index = prev_want;
    free_scratch(&scratch);
    errorGL();
}
AliasExport(void, glMultiDrawElements, ,
            (GLenum mode, GLsizei* count, GLenum type, const void* const* indices, GLsizei primcount));
AliasExport(void, glMultiDrawElements, EXT,
            (GLenum mode, GLsizei* count, GLenum type, const void* const* indices, GLsizei primcount));

static void (*internal_glDrawElementsBaseVertex)(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                                 GLint basevertex);

void internal_glDrawElementsBaseVertex_gles30(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                              GLint basevertex) {
    DBG(SHUT_LOGD("internal_glDrawElementsBaseVertex_gles30(%s, %d, %s, %p, %d), inlist=%i, pending=%d\n",
                  PrintEnum(mode), count, PrintEnum(type), indices, basevertex, (glstate->list.active) ? 1 : 0,
                  glstate->list.pending);)
    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);
    // BATCH Mode
    GLsizei maxcount = count;
    GLsizei mincount = count;
    if (count > maxcount) maxcount = count;
    if (count < mincount) mincount = count;
    if (!compiling) {
        if (!intercept && glstate->list.pending &&
            maxcount > MAX_BATCH) // too large and will not intercept, stop the BATCH
            gl4es_flush();
        else if ((!intercept && !glstate->list.pending && mincount < MIN_BATCH) || (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }
    renderlist_t* list = NULL;
    GLsizei real_count = adjust_vertices(mode, count);

    if (real_count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (real_count == 0) {
        noerrorShim();
        return;
    }

    noerrorShim();
    GLushort* sindices = NULL;
    GLuint* iindices = NULL;

    const void* current_indices =
        (glstate->vao->elements) ? (void*)((char*)glstate->vao->elements->data + (uintptr_t)indices) : indices;

    if (type == GL_UNSIGNED_INT && hardext.elementuint && !compiling && !intercept) {
        iindices = copy_gl_array(current_indices, type, 1, 0, GL_UNSIGNED_INT, 1, 0, real_count, NULL);
    } else {
        sindices = copy_gl_array(current_indices, type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, real_count, NULL);
    }

    if (compiling) {
        renderlist_t* active_list = glstate->list.active;
        GLsizei min, max;

        NewStage(active_list, STAGE_DRAW);

        normalize_indices_us(sindices, &max, &min, real_count);
        active_list = arrays_to_renderlist(active_list, mode, min + basevertex, max + basevertex + 1);
        active_list->indices = sindices;
        active_list->ilen = real_count;
        active_list->indice_cap = real_count;
        glstate->list.active = active_list;

        if (glstate->list.pending) {
            NewStage(glstate->list.active, STAGE_POSTDRAW);
        } else {
            glstate->list.active = extend_renderlist(glstate->list.active);
        }
        return;
    }

    if (intercept) {
        GLsizei min, max;

        normalize_indices_us(sindices, &max, &min, real_count);
        if (list) {
            NewStage(list, STAGE_DRAW);
        }
        list = arrays_to_renderlist(list, mode, min + basevertex, max + basevertex + 1);
        list->indices = sindices;
        list->ilen = real_count;
        list->indice_cap = real_count;
        return;
    } else {
        if (iindices) {
            for (int j = 0; j < real_count; j++) {
                iindices[j] += basevertex;
            }
        } else {
            for (int j = 0; j < real_count; j++) {
                sindices[j] += basevertex;
            }
        }
        GLuint old_index = wantBufferIndex(0);
        glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, 1);
        if (iindices) {
            free(iindices);
        } else {
            free(sindices);
        }
        wantBufferIndex(old_index);
    }
    if (list) {
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    }
}

void internal_glDrawElementsBaseVertex_gles32(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                              GLint basevertex) {
    DBG(SHUT_LOGD("internal_glDrawElementsBaseVertex_gles30(%s, %d, %s, %p, %d), inlist=%i, pending=%d\n",
                  PrintEnum(mode), count, PrintEnum(type), indices, basevertex, (glstate->list.active) ? 1 : 0,
                  glstate->list.pending);)
    LOAD_GLES3_OR_EXT(glDrawElementsBaseVertex);
    if(!gles_glDrawElementsBaseVertex) {
        DBG(SHUT_LOGD("ERR: glDrawElementsBaseVertex not available\n"));
        return;
    }
    gles_glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
}

void APIENTRY_GL4ES gl4es_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                                   GLint basevertex) {
    DBG(SHUT_LOGD("glDrawElementsBaseVertex(%s, %d, %s, %p, %d), vtx=%p map=%p, pending=%d\n", PrintEnum(mode), count,
                  PrintEnum(type), indices, basevertex, (glstate->vao->vertex) ? glstate->vao->vertex->data : NULL,
                  (glstate->vao->elements) ? glstate->vao->elements->data : NULL, glstate->list.pending);)
    if (mode == GL_QUADS || mode == GL_QUAD_STRIP || mode == GL_POLYGON ||
        glstate->list.active || should_intercept_render(mode) ||
        !glstate->vao->elements || !glstate->vao->elements->real_buffer) {
        internal_glDrawElementsBaseVertex_gles30(mode, count, type, indices, basevertex);
        return;
    }
    scratch_t scratch = {0};

    realize_glenv(mode == GL_POINTS, 0, 0, type, indices, &scratch);
    free_scratch(&scratch);

    realize_textures(1);
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, glstate->vao->elements->real_buffer);
    internal_glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
}
AliasExport(void, glDrawElementsBaseVertex, ,
            (GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex));
AliasExport(void, glDrawElementsBaseVertex, ARB,
            (GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex));

typedef struct {
    GLuint count;
    GLuint instanceCount;
    GLuint first;
    GLuint baseInstance;
} EmesisDrawArraysIndirectCmd;

typedef struct {
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLint  baseVertex;
    GLuint baseInstance;
} EmesisDrawElementsIndirectCmd;

typedef struct {
    int idx;
    const char* base;
    size_t buf_size;
    int has_buf;
    int stride;
    int elem_bytes;
    GLint size;
    GLenum type;
    GLboolean normalized;
    int integer;
    GLuint divisor;
    GLfloat last_val[4];
    int has_last;
} EmesisInstancedAttribInfo;

static int emesis_collect_instanced_attribs(EmesisInstancedAttribInfo* out_info, int max_out) {
    int n = 0;
    if (!glstate || !glstate->vao) return 0;
    for (int a = 0; a < hardext.maxvattrib && n < max_out; ++a) {
        vertexattrib_t* w = &glstate->vao->vertexattrib[a];
        if (!w->enabled || w->divisor == 0) continue;
        if (w->buffer) {
            w->buffer->used_by_divisor = 1;
            if (!w->buffer->data) {
                emesis_ensure_buffer_cpu_shadow(w->buffer, GL_ARRAY_BUFFER, 1);
            }
            if (!w->buffer->data) continue;
        }
        int elem_bytes = gl_sizeof(w->type) * ((w->size == GL_BGRA) ? 4 : w->size);
        int stride = w->stride ? w->stride : elem_bytes;
        out_info[n].idx = a;
        out_info[n].base = (const char*)((uintptr_t)w->pointer + (w->buffer ? (uintptr_t)w->buffer->data : 0));
        out_info[n].buf_size = w->buffer ? (size_t)w->buffer->size : 0;
        out_info[n].has_buf = (w->buffer != NULL);
        out_info[n].stride = stride;
        out_info[n].elem_bytes = elem_bytes;
        out_info[n].size = w->size;
        out_info[n].type = w->type;
        out_info[n].normalized = w->normalized;
        out_info[n].integer = w->integer;
        out_info[n].divisor = w->divisor;
        out_info[n].has_last = 0;
        ++n;
    }
    return n;
}

static inline void emesis_apply_instanced_attribs(EmesisInstancedAttribInfo* infos, int n_infos,
                                                  GLuint instance_idx, GLuint base_instance,
                                                  glVertexAttrib4fv_PTR fn_va4fv) {
    for (int k = 0; k < n_infos; ++k) {
        EmesisInstancedAttribInfo* inf = &infos[k];
        if (!inf->base || inf->stride <= 0) continue;
        size_t slot = (size_t)(instance_idx / inf->divisor) + (size_t)base_instance;
        size_t byte_off = slot * (size_t)inf->stride;
        if (inf->has_buf && ((uintptr_t)inf->base < (uintptr_t)glstate->vao->vertexattrib[inf->idx].buffer->data ||
                             ((size_t)((uintptr_t)inf->base -
                                       (uintptr_t)glstate->vao->vertexattrib[inf->idx].buffer->data) +
                              byte_off + (size_t)inf->elem_bytes) > inf->buf_size)) {
            continue;
        }
        const char* src = inf->base + byte_off;
        if (inf->type == GL_FLOAT && inf->size == 4 && !inf->integer) {
            const GLfloat* fp = (const GLfloat*)src;
            if (!inf->has_last || memcmp(inf->last_val, fp, 4 * sizeof(GLfloat)) != 0) {
                fn_va4fv(inf->idx, fp);
                memcpy(inf->last_val, fp, 4 * sizeof(GLfloat));
                inf->has_last = 1;
            }
        } else {
            GLfloat tmp[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            if (inf->type == GL_FLOAT) {
                memcpy(tmp, src, (size_t)inf->size * sizeof(GLfloat));
            } else if (inf->type == GL_DOUBLE || !inf->normalized || inf->integer) {
                const char* cur = src;
                for (int c = 0; c < inf->size && c < 4; ++c) {
                    GL_TYPE_SWITCH(input, cur, inf->type, tmp[c] = (GLfloat)input[c];, )
                }
            } else {
                const char* cur = src;
                for (int c = 0; c < inf->size && c < 4; ++c) {
                    GL_TYPE_SWITCH_MAX(input, cur, inf->type, tmp[c] = (GLfloat)input[c] / (GLfloat)maxv;, )
                }
            }
            if (!inf->has_last || memcmp(inf->last_val, tmp, 4 * sizeof(GLfloat)) != 0) {
                fn_va4fv(inf->idx, tmp);
                memcpy(inf->last_val, tmp, 4 * sizeof(GLfloat));
                inf->has_last = 1;
            }
        }
    }
}

static inline void emesis_commit_instanced_attribs(const EmesisInstancedAttribInfo* infos, int n_infos) {
    if (!glstate || !glstate->gleshard) return;
    for (int k = 0; k < n_infos; ++k) {
        if (infos[k].has_last) {
            memcpy(glstate->gleshard->vavalue[infos[k].idx], infos[k].last_val, 4 * sizeof(GLfloat));
        }
    }
}

void APIENTRY_GL4ES gl4es_glMultiDrawArraysIndirect(GLenum mode, const void* indirect, GLsizei drawcount,
                                                    GLsizei stride) {
    DBG(SHUT_LOGD("glMultiDrawArraysIndirect(%s, %p, %d, %d)\n", PrintEnum(mode), indirect, drawcount, stride);)
    if (drawcount <= 0) {
        if (drawcount < 0) errorShim(GL_INVALID_VALUE);
        else noerrorShim();
        return;
    }
    if (stride < 0 || (stride != 0 && (stride & 3) != 0)) {
        errorShim(GL_INVALID_VALUE);
        return;
    }

    size_t cmd_stride = (stride == 0) ? sizeof(EmesisDrawArraysIndirectCmd) : (size_t)stride;
    glbuffer_t* ind_buf = (glstate->defaultvao) ? glstate->defaultvao->indirect : glstate->vao->indirect;
    const uint8_t* cmd_base = NULL;
    if (ind_buf) {
        if (!ind_buf->data) emesis_ensure_buffer_cpu_shadow(ind_buf, GL_DRAW_INDIRECT_BUFFER, 1);
        size_t byte_off = (size_t)(uintptr_t)indirect;
        size_t req_end = byte_off + (size_t)(drawcount - 1) * cmd_stride + sizeof(EmesisDrawArraysIndirectCmd);
        if (!ind_buf->data || byte_off > (size_t)ind_buf->size || req_end > (size_t)ind_buf->size) {
            errorShim(GL_INVALID_OPERATION);
            return;
        }
        cmd_base = (const uint8_t*)ind_buf->data + byte_off;
    } else {
        if (!indirect) {
            errorShim(GL_INVALID_OPERATION);
            return;
        }
        cmd_base = (const uint8_t*)indirect;
    }

    if (glstate->list.pending) gl4es_flush();

    bool compiling = (glstate->list.active != NULL);
    bool intercept = should_intercept_render(mode);
    GLenum draw_mode = (glstate->polygon_mode == GL_POINT && mode >= GL_TRIANGLES) ? GL_POINTS : mode;

    /* High-speed GPU batch path for Sodium MultidrawChunkRenderBackend & Iris terrain/shadow passes */
    if (!compiling && !intercept && glstate->polygon_mode != GL_LINE && glstate->render_mode != GL_SELECT &&
        emesis_vao_has_only_vbo_attribs(1)) {
        LOAD_GLES2(glVertexAttrib4fv);
        EmesisInstancedAttribInfo inst_infos[MAX_VATTRIB];
        int n_inst = emesis_collect_instanced_attribs(inst_infos, MAX_VATTRIB);

        if (draw_mode == GL_QUADS) {
            typedef struct {
                GLint first;
                GLsizei nquads;
                GLuint inst_cnt;
                GLuint base_inst;
            } QuadIndRun;

            QuadIndRun stack_runs[512];
            QuadIndRun* runs = (drawcount <= 512)
                                   ? stack_runs
                                   : (QuadIndRun*)malloc((size_t)drawcount * sizeof(QuadIndRun));
            if (runs) {
                int n_runs = 0;
                size_t max_vert = 0;
                int all_aligned = 1;
                for (GLsizei i = 0; i < drawcount; ++i) {
                    EmesisDrawArraysIndirectCmd cmd;
                    memcpy(&cmd, cmd_base + (size_t)i * cmd_stride, sizeof(cmd));
                    GLsizei q = (GLsizei)(cmd.count / 4u);
                    if (q <= 0 || cmd.instanceCount == 0) continue;
                    GLint f = (GLint)cmd.first;
                    if (f < 0) continue;
                    size_t end_v = (size_t)f + (size_t)q * 4u;
                    if (end_v > max_vert) max_vert = end_v;

                    if (n_runs > 0 && cmd.instanceCount == 1 && runs[n_runs - 1].inst_cnt == 1 &&
                        runs[n_runs - 1].base_inst == cmd.baseInstance &&
                        (size_t)runs[n_runs - 1].first + (size_t)runs[n_runs - 1].nquads * 4u == (size_t)f) {
                        runs[n_runs - 1].nquads += q;
                    } else {
                        if (f & 3) all_aligned = 0;
                        runs[n_runs].first = f;
                        runs[n_runs].nquads = q;
                        runs[n_runs].inst_cnt = cmd.instanceCount;
                        runs[n_runs].base_inst = cmd.baseInstance;
                        ++n_runs;
                    }
                }

                if (n_runs == 0) {
                    if (runs != stack_runs) free(runs);
                    noerrorShim();
                    return;
                }

                int can_bv = 0;
                LOAD_GLES3_OR_EXT(glDrawElementsBaseVertex);
                if (hardext.basevertex && gles_glDrawElementsBaseVertex && ensure_gpu_quad_ibo_u16(16384)) {
                    can_bv = 1;
                }
                int can_ibo = 0;
                int use_u32 = (max_vert > 65536);
                if (!can_bv && all_aligned &&
                    ((!use_u32 && ensure_gpu_quad_ibo_u16(max_vert >> 2)) ||
                     (use_u32 && ensure_gpu_quad_ibo_u32(max_vert >> 2)))) {
                    can_ibo = 1;
                }

                if (can_bv || can_ibo) {
                    if (glstate->raster.bm_drawing) bitmap_flush();
                    glbuffer_t* qibo = (!can_bv && use_u32) ? &glstate->quad_ibo_u32 : &glstate->quad_ibo_u16;
                    size_t idx_bytes = (!can_bv && use_u32) ? sizeof(GLuint) : sizeof(GLushort);
                    GLenum idx_type = (!can_bv && use_u32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;

                    glbuffer_t* prev_elem = glstate->vao->elements;
                    GLuint prev_want = glstate->bind_buffer.want_index;
                    GLuint prev_inst_id = glstate->instanceID;
                    glstate->vao->elements = qibo;

                    realize_textures(1);
                    scratch_t scratch = {0};
                    realize_glenv(0, 0, (int)max_vert, idx_type, NULL, &scratch);
                    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, qibo->real_buffer);
                    LOAD_GLES2(glDrawElements);

                    GLuint cur_base_inst = 0xFFFFFFFFu;
                    for (int r = 0; r < n_runs; ++r) {
                        const QuadIndRun* rn = &runs[r];
                        for (GLuint inst = 0; inst < rn->inst_cnt; ++inst) {
                            if (n_inst > 0 && (rn->inst_cnt > 1 || rn->base_inst != cur_base_inst)) {
                                emesis_apply_instanced_attribs(inst_infos, n_inst, inst, rn->base_inst,
                                                               gles_glVertexAttrib4fv);
                                cur_base_inst = (rn->inst_cnt == 1) ? rn->base_inst : 0xFFFFFFFFu;
                            }
                            if (can_bv) {
                                GLsizei rem_q = rn->nquads;
                                GLint cur_first = rn->first;
                                while (rem_q > 0) {
                                    GLsizei cq = (rem_q > 16384) ? 16384 : rem_q;
                                    gles_glDrawElementsBaseVertex(GL_TRIANGLES, cq * 6, GL_UNSIGNED_SHORT, NULL,
                                                                  cur_first);
                                    cur_first += cq * 4;
                                    rem_q -= cq;
                                }
                            } else {
                                gles_glDrawElements(GL_TRIANGLES, rn->nquads * 6, idx_type,
                                                    (const void*)(((size_t)rn->first >> 2) * 6u * idx_bytes));
                            }
                        }
                    }

                    emesis_commit_instanced_attribs(inst_infos, n_inst);
                    free_scratch(&scratch);
                    glstate->instanceID = prev_inst_id;
                    glstate->vao->elements = prev_elem;
                    glstate->bind_buffer.want_index = prev_want;
                    if (runs != stack_runs) free(runs);
                    errorGL();
                    return;
                }
                if (runs != stack_runs) free(runs);
            }
        } else if (draw_mode != GL_QUAD_STRIP && draw_mode != GL_POLYGON) {
            if (glstate->raster.bm_drawing) bitmap_flush();
            LOAD_GLES(glDrawArrays);
            realize_textures(1);
            scratch_t scratch = {0};
            realize_glenv(draw_mode == GL_POINTS, 0, 0, 0, NULL, &scratch);

            int can_merge = (draw_mode == GL_TRIANGLES || draw_mode == GL_LINES || draw_mode == GL_POINTS);
            GLuint cur_base_inst = 0xFFFFFFFFu;
            GLsizei i = 0;
            while (i < drawcount) {
                EmesisDrawArraysIndirectCmd cmd;
                memcpy(&cmd, cmd_base + (size_t)i * cmd_stride, sizeof(cmd));
                GLsizei c = adjust_vertices(draw_mode, (GLsizei)cmd.count);
                ++i;
                if (c <= 0 || cmd.instanceCount == 0) continue;
                GLint f = (GLint)cmd.first;
                if (can_merge && cmd.instanceCount == 1) {
                    while (i < drawcount) {
                        EmesisDrawArraysIndirectCmd nxt;
                        memcpy(&nxt, cmd_base + (size_t)i * cmd_stride, sizeof(nxt));
                        GLsizei nc = adjust_vertices(draw_mode, (GLsizei)nxt.count);
                        if (nc <= 0 || nxt.instanceCount == 0) {
                            ++i;
                            continue;
                        }
                        if (nxt.instanceCount == 1 && nxt.baseInstance == cmd.baseInstance &&
                            (GLuint)(f + c) == nxt.first) {
                            c += nc;
                            ++i;
                        } else {
                            break;
                        }
                    }
                }
                for (GLuint inst = 0; inst < cmd.instanceCount; ++inst) {
                    if (n_inst > 0 && (cmd.instanceCount > 1 || cmd.baseInstance != cur_base_inst)) {
                        emesis_apply_instanced_attribs(inst_infos, n_inst, inst, cmd.baseInstance,
                                                       gles_glVertexAttrib4fv);
                        cur_base_inst = (cmd.instanceCount == 1) ? cmd.baseInstance : 0xFFFFFFFFu;
                    }
                    gles_glDrawArrays(draw_mode, f, c);
                }
            }

            emesis_commit_instanced_attribs(inst_infos, n_inst);
            free_scratch(&scratch);
            errorGL();
            return;
        }
    }

    /* General fallback (display lists, GL_LINE polygon mode, client arrays, GL_QUAD_STRIP/GL_POLYGON) */
    GLuint prev_inst_id = glstate->instanceID;
    for (GLsizei i = 0; i < drawcount; ++i) {
        EmesisDrawArraysIndirectCmd cmd;
        memcpy(&cmd, cmd_base + (size_t)i * cmd_stride, sizeof(cmd));
        if (cmd.count == 0 || cmd.instanceCount == 0) continue;
        for (GLuint inst = 0; inst < cmd.instanceCount; ++inst) {
            glstate->instanceID = inst + cmd.baseInstance;
            gl4es_glDrawArrays(mode, (GLint)cmd.first, (GLsizei)cmd.count);
        }
    }
    glstate->instanceID = prev_inst_id;
}
AliasExport(void, glMultiDrawArraysIndirect, ,
            (GLenum mode, const void* indirect, GLsizei drawcount, GLsizei stride));
AliasExport(void, glMultiDrawArraysIndirect, ARB,
            (GLenum mode, const void* indirect, GLsizei drawcount, GLsizei stride));

void APIENTRY_GL4ES gl4es_glDrawArraysIndirect(GLenum mode, const void* indirect) {
    gl4es_glMultiDrawArraysIndirect(mode, indirect, 1, 0);
}
AliasExport(void, glDrawArraysIndirect, , (GLenum mode, const void* indirect));
AliasExport(void, glDrawArraysIndirect, ARB, (GLenum mode, const void* indirect));

void APIENTRY_GL4ES gl4es_glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect,
                                                      GLsizei drawcount, GLsizei stride) {
    DBG(SHUT_LOGD("glMultiDrawElementsIndirect(%s, %s, %p, %d, %d)\n", PrintEnum(mode), PrintEnum(type), indirect,
                  drawcount, stride);)
    if (drawcount <= 0) {
        if (drawcount < 0) errorShim(GL_INVALID_VALUE);
        else noerrorShim();
        return;
    }
    if (stride < 0 || (stride != 0 && (stride & 3) != 0)) {
        errorShim(GL_INVALID_VALUE);
        return;
    }

    size_t cmd_stride = (stride == 0) ? sizeof(EmesisDrawElementsIndirectCmd) : (size_t)stride;
    glbuffer_t* ind_buf = (glstate->defaultvao) ? glstate->defaultvao->indirect : glstate->vao->indirect;
    const uint8_t* cmd_base = NULL;
    if (ind_buf) {
        if (!ind_buf->data) emesis_ensure_buffer_cpu_shadow(ind_buf, GL_DRAW_INDIRECT_BUFFER, 1);
        size_t byte_off = (size_t)(uintptr_t)indirect;
        size_t req_end = byte_off + (size_t)(drawcount - 1) * cmd_stride + sizeof(EmesisDrawElementsIndirectCmd);
        if (!ind_buf->data || byte_off > (size_t)ind_buf->size || req_end > (size_t)ind_buf->size) {
            errorShim(GL_INVALID_OPERATION);
            return;
        }
        cmd_base = (const uint8_t*)ind_buf->data + byte_off;
    } else {
        if (!indirect) {
            errorShim(GL_INVALID_OPERATION);
            return;
        }
        cmd_base = (const uint8_t*)indirect;
    }

    if (glstate->list.pending) gl4es_flush();

    size_t idx_bytes = (type == GL_UNSIGNED_INT) ? 4u : ((type == GL_UNSIGNED_SHORT) ? 2u : 1u);
    GLenum draw_mode = (glstate->polygon_mode == GL_POINT && mode >= GL_TRIANGLES) ? GL_POINTS : mode;

    if (!glstate->list.active && !should_intercept_render(mode) && glstate->polygon_mode != GL_LINE &&
        glstate->render_mode != GL_SELECT && draw_mode != GL_QUADS && draw_mode != GL_QUAD_STRIP &&
        draw_mode != GL_POLYGON && type != GL_UNSIGNED_BYTE &&
        (type != GL_UNSIGNED_INT || hardext.elementuint) &&
        glstate->vao && glstate->vao->elements && glstate->vao->elements->real_buffer &&
        emesis_vao_has_only_vbo_attribs(1)) {
        LOAD_GLES2(glVertexAttrib4fv);
        LOAD_GLES2(glDrawElements);
        LOAD_GLES3_OR_EXT(glDrawElementsBaseVertex);
        EmesisInstancedAttribInfo inst_infos[MAX_VATTRIB];
        int n_inst = emesis_collect_instanced_attribs(inst_infos, MAX_VATTRIB);

        int need_bv = 0;
        for (GLsizei i = 0; i < drawcount; ++i) {
            EmesisDrawElementsIndirectCmd cmd;
            memcpy(&cmd, cmd_base + (size_t)i * cmd_stride, sizeof(cmd));
            if (cmd.count > 0 && cmd.instanceCount > 0 && cmd.baseVertex != 0) {
                need_bv = 1;
                break;
            }
        }

        if (!need_bv || (hardext.basevertex && gles_glDrawElementsBaseVertex)) {
            if (glstate->raster.bm_drawing) bitmap_flush();
            realize_textures(1);
            scratch_t scratch = {0};
            realize_glenv(draw_mode == GL_POINTS, 0, 0, type, NULL, &scratch);
            bindBuffer(GL_ELEMENT_ARRAY_BUFFER, glstate->vao->elements->real_buffer);

            GLuint cur_base_inst = 0xFFFFFFFFu;
            for (GLsizei i = 0; i < drawcount; ++i) {
                EmesisDrawElementsIndirectCmd cmd;
                memcpy(&cmd, cmd_base + (size_t)i * cmd_stride, sizeof(cmd));
                GLsizei c = adjust_vertices(draw_mode, (GLsizei)cmd.count);
                if (c <= 0 || cmd.instanceCount == 0) continue;
                const void* idx_off = (const void*)((size_t)cmd.firstIndex * idx_bytes);
                for (GLuint inst = 0; inst < cmd.instanceCount; ++inst) {
                    if (n_inst > 0 && (cmd.instanceCount > 1 || cmd.baseInstance != cur_base_inst)) {
                        emesis_apply_instanced_attribs(inst_infos, n_inst, inst, cmd.baseInstance,
                                                       gles_glVertexAttrib4fv);
                        cur_base_inst = (cmd.instanceCount == 1) ? cmd.baseInstance : 0xFFFFFFFFu;
                    }
                    if (cmd.baseVertex != 0) {
                        gles_glDrawElementsBaseVertex(draw_mode, c, type, idx_off, cmd.baseVertex);
                    } else {
                        gles_glDrawElements(draw_mode, c, type, idx_off);
                    }
                }
            }

            emesis_commit_instanced_attribs(inst_infos, n_inst);
            free_scratch(&scratch);
            errorGL();
            return;
        }
    }

    GLuint prev_inst_id = glstate->instanceID;
    for (GLsizei i = 0; i < drawcount; ++i) {
        EmesisDrawElementsIndirectCmd cmd;
        memcpy(&cmd, cmd_base + (size_t)i * cmd_stride, sizeof(cmd));
        if (cmd.count == 0 || cmd.instanceCount == 0) continue;
        const void* idx_off = (const void*)((size_t)cmd.firstIndex * idx_bytes);
        for (GLuint inst = 0; inst < cmd.instanceCount; ++inst) {
            glstate->instanceID = inst + cmd.baseInstance;
            if (cmd.baseVertex == 0) {
                gl4es_glDrawElements(mode, (GLsizei)cmd.count, type, idx_off);
            } else {
                gl4es_glDrawElementsInstancedBaseVertex(mode, (GLsizei)cmd.count, type, idx_off, 1, cmd.baseVertex);
            }
        }
    }
    glstate->instanceID = prev_inst_id;
}
AliasExport(void, glMultiDrawElementsIndirect, ,
            (GLenum mode, GLenum type, const void* indirect, GLsizei drawcount, GLsizei stride));
AliasExport(void, glMultiDrawElementsIndirect, ARB,
            (GLenum mode, GLenum type, const void* indirect, GLsizei drawcount, GLsizei stride));

void APIENTRY_GL4ES gl4es_glDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect) {
    gl4es_glMultiDrawElementsIndirect(mode, type, indirect, 1, 0);
}
AliasExport(void, glDrawElementsIndirect, , (GLenum mode, GLenum type, const void* indirect));
AliasExport(void, glDrawElementsIndirect, ARB, (GLenum mode, GLenum type, const void* indirect));

void APIENTRY_GL4ES gl4es_glMemoryBarrier(GLbitfield barriers) {
    LOAD_GLES3(glMemoryBarrier);
    if (gles_glMemoryBarrier) gles_glMemoryBarrier(barriers);
}
AliasExport(void, glMemoryBarrier, , (GLbitfield barriers));
AliasExport(void, glMemoryBarrier, EXT, (GLbitfield barriers));

void internal_glMultiDrawElementsBaseVertex_gles32(GLenum mode, const GLsizei* counts, GLenum type,
                                                   const void* const* indices, GLsizei primcount,
                                                   const GLint* basevertex) {
    LOAD_GLES3_OR_EXT(glDrawElementsBaseVertex);
    scratch_t scratch = {0};
    realize_glenv(mode == GL_POINTS, 0, 0, type, indices, &scratch);
    free_scratch(&scratch);

    realize_textures(1);
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, glstate->vao->elements->real_buffer);

    DBG(SHUT_LOGD("gles_glDrawElementsBaseVertex loop x%d\n", primcount);)
    for (int i = 0; i < primcount; i++) {
        if (counts[i] > 0) gles_glDrawElementsBaseVertex(mode, counts[i], type, indices[i], basevertex[i]);
    }
}

void internal_glMultiDrawElementsBaseVertex_gles30(GLenum mode, const GLsizei* counts, GLenum type,
                                                   const void* const* indices, GLsizei primcount,
                                                   const GLint* basevertex) {
    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);
    // BATCH Mode
    GLsizei maxcount = counts[0];
    GLsizei mincount = counts[0];
    for (int i = 1; i < primcount; i++) {
        if (counts[i] > maxcount) maxcount = counts[i];
        if (counts[i] < mincount) mincount = counts[i];
    }
    if (!compiling) {
        if (!intercept && glstate->list.pending &&
            maxcount > MAX_BATCH) // too large and will not intercept, stop the BATCH
            gl4es_flush();
        else if ((!intercept && !glstate->list.pending && mincount < MIN_BATCH) || (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }
    renderlist_t* list = NULL;
    for (int i = 0; i < primcount; i++) {
        GLsizei count = adjust_vertices(mode, counts[i]);

        if (count < 0) {
            errorShim(GL_INVALID_VALUE);
            continue;
        }
        if (count == 0) {
            noerrorShim();
            continue;
        }

        noerrorShim();
        GLushort* sindices = NULL;
        GLuint* iindices = NULL;

        // Get the pointer to the indices for the current draw call.
        const void* current_indices = (glstate->vao->elements)
                                          ? (void*)((char*)glstate->vao->elements->data + (uintptr_t)indices[i])
                                          : indices[i];

        // The direct path can use uint indices if supported.
        // Compiling and intercepting paths will always convert indices to ushort for the renderlist.
        if (type == GL_UNSIGNED_INT && hardext.elementuint && !compiling && !intercept) {
            iindices = copy_gl_array(current_indices, type, 1, 0, GL_UNSIGNED_INT, 1, 0, count, NULL);
        } else {
            sindices = copy_gl_array(current_indices, type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);
        }

        if (compiling) {
            // This path always works with 'sindices' (ushort).
            // Any uint indices are converted to ushort in the logic above.
            renderlist_t* active_list = glstate->list.active;
            GLsizei min, max;

            NewStage(active_list, STAGE_DRAW);

            normalize_indices_us(sindices, &max, &min, count);
            // The min/max indices are offset by basevertex to fetch the correct range of vertex data.
            active_list = arrays_to_renderlist(active_list, mode, min + basevertex[i], max + basevertex[i] + 1);
            active_list->indices = sindices;
            active_list->ilen = count;
            active_list->indice_cap = count;
            glstate->list.active = active_list;

            if (glstate->list.pending) {
                NewStage(glstate->list.active, STAGE_POSTDRAW);
            } else {
                glstate->list.active = extend_renderlist(glstate->list.active);
            }
            continue;
        }

        if (intercept) {
            // This path also always works with 'sindices'.
            GLsizei min, max;

            normalize_indices_us(sindices, &max, &min, count);
            if (list) {
                NewStage(list, STAGE_DRAW);
            }
            // Create or append to a temporary list for this MultiDraw call.
            list = arrays_to_renderlist(list, mode, min + basevertex[i], max + basevertex[i] + 1);
            list->indices = sindices; // The list now owns the sindices pointer
            list->ilen = count;
            list->indice_cap = count;
            continue;
        } else {
            // Direct rendering path.
            if (iindices) {
                for (int j = 0; j < count; j++) {
                    iindices[j] += basevertex[i];
                }
            } else {
                for (int j = 0; j < count; j++) {
                    sindices[j] += basevertex[i];
                }
            }
            GLuint old_index = wantBufferIndex(0);
            glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, 1);

            if (iindices) {
                free(iindices);
            } else {
                free(sindices);
            }
            wantBufferIndex(old_index);
        }
    }
    if (list) {
        // If we were intercepting, draw the accumulated list now.
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    }
}

static void (*internal_glMultiDrawElementsBaseVertex)(GLenum mode, const GLsizei* counts, GLenum type,
                                                      const void* const* indices, GLsizei primcount,
                                                      const GLint* basevertex);

void init_internal_glDrawElementsBaseVertex() {
    if (hardext.basevertex) {
        LOGD("Switch to native basevertex implementation");
        internal_glDrawElementsBaseVertex = internal_glDrawElementsBaseVertex_gles32;
        internal_glMultiDrawElementsBaseVertex = internal_glMultiDrawElementsBaseVertex_gles32;
    } else {
        LOGD("Switch to emulated basevertex implementation");
        internal_glDrawElementsBaseVertex = internal_glDrawElementsBaseVertex_gles30;
        internal_glMultiDrawElementsBaseVertex = internal_glMultiDrawElementsBaseVertex_gles30;
    }

    if (hardext.multidraw && hardext.basevertex) {
        LOGD("Extension GL_EXT_draw_elements_base_vertex and GL_EXT_multi_draw_arrays detected and "
             "glMultiDrawElementsBaseVertexEXT used");
    }
}

void gl4es_glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei* counts, GLenum type, const void* const* indices,
                                         GLsizei primcount, const GLint* basevertex) {
    DBG(SHUT_LOGD("glMultiDrawElementsBaseVertex(%s, %p, %s, @%p, %d, @%p), inlist=%i, pending=%d\n", PrintEnum(mode),
                  counts, PrintEnum(type), indices, primcount, basevertex, (glstate->list.active) ? 1 : 0,
                  glstate->list.pending);)
    if (primcount <= 0) {
        if (primcount < 0) errorShim(GL_INVALID_VALUE);
        else noerrorShim();
        return;
    }
    if (glstate->list.active || should_intercept_render(mode) || mode == GL_QUADS || mode == GL_QUAD_STRIP ||
        mode == GL_POLYGON || glstate->polygon_mode == GL_LINE || type == GL_UNSIGNED_BYTE) {
        for (int i = 0; i < primcount; i++) {
            if (counts[i] > 0) gl4es_glDrawElementsBaseVertex(mode, counts[i], type, indices[i], basevertex[i]);
        }
        return;
    }
    if (glstate->list.pending) gl4es_flush();

    GLenum draw_mode = (glstate->polygon_mode == GL_POINT && mode >= GL_TRIANGLES) ? GL_POINTS : mode;
    GLuint elem_buf = (glstate->vao && glstate->vao->elements) ? glstate->vao->elements->real_buffer : 0;
    const void* first_cpu_idx = NULL;
    if (indices) {
        if (elem_buf && glstate->vao->elements->data) {
            first_cpu_idx = (const void*)((const char*)glstate->vao->elements->data + (uintptr_t)indices[0]);
        } else if (!elem_buf) {
            first_cpu_idx = indices[0];
        }
    }

    scratch_t scratch = {0};
    realize_glenv(draw_mode == GL_POINTS, 0, counts ? counts[0] : 0, first_cpu_idx ? type : 0, first_cpu_idx, &scratch);

    realize_textures(1);
    GLuint prev_want = glstate->bind_buffer.want_index;
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, elem_buf);

    if (hardext.multidraw && hardext.basevertex && elem_buf) {
        LOAD_GLES_EXT(glMultiDrawElementsBaseVertex);
        if (gles_glMultiDrawElementsBaseVertex) {
            gles_glMultiDrawElementsBaseVertex(draw_mode, counts, type, indices, primcount, basevertex);
            glstate->bind_buffer.want_index = prev_want;
            free_scratch(&scratch);
            errorGL();
            return;
        }
    }
    internal_glMultiDrawElementsBaseVertex(draw_mode, counts, type, indices, primcount, basevertex);
    glstate->bind_buffer.want_index = prev_want;
    free_scratch(&scratch);
}
AliasExport(void, glMultiDrawElementsBaseVertex, ,
            (GLenum mode, GLsizei* counts, GLenum type, const void* const* indices, GLsizei primcount,
             const GLint* basevertex));
AliasExport(void, glMultiDrawElementsBaseVertex, ARB,
            (GLenum mode, GLsizei* counts, GLenum type, const void* const* indices, GLsizei primcount,
             const GLint* basevertex));

void APIENTRY_GL4ES gl4es_glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count,
                                                        GLenum type, const void* indices, GLint basevertex) {
    DBG(SHUT_LOGD("glDrawRangeElementsBaseVertex(%s, %i, %i, %i, %s, @%p, %d), inlist=%i, pending=%d\n",
                  PrintEnum(mode), start, end, count, PrintEnum(type), indices, basevertex,
                  (glstate->list.active) ? 1 : 0, glstate->list.pending);)
    if (basevertex == 0) {
        gl4es_glDrawRangeElements(mode, start, end, count, type, indices);
    } else {
        count = adjust_vertices(mode, count);

        if (count < 0) {
            errorShim(GL_INVALID_VALUE);
            return;
        }
        if (count == 0) {
            noerrorShim();
            return;
        }

        bool compiling = (glstate->list.active);
        bool intercept = should_intercept_render(mode);

        // BATCH Mode
        if (!compiling) {
            if (!intercept && glstate->list.pending &&
                count > MAX_BATCH) // too large and will not intercept, stop the BATCH
                gl4es_flush();
            else if ((!intercept && !glstate->list.pending && count < MIN_BATCH) ||
                     (intercept && globals4es.maxbatch)) {
                compiling = true;
                glstate->list.pending = 1;
                glstate->list.active = alloc_renderlist();
            }
        }

        noerrorShim();
        GLushort* sindices = NULL;
        GLuint* iindices = NULL;
        if (type == GL_UNSIGNED_INT && hardext.elementuint && !compiling && !intercept)
            iindices = copy_gl_array(
                (glstate->vao->elements) ? (void*)((char*)glstate->vao->elements->data + (uintptr_t)indices) : indices,
                type, 1, 0, GL_UNSIGNED_INT, 1, 0, count, NULL);
        else
            sindices = copy_gl_array(
                (glstate->vao->elements) ? (void*)((char*)glstate->vao->elements->data + (uintptr_t)indices) : indices,
                type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);

        if (compiling) {
            // TODO, handle uint indices
            renderlist_t* list = NULL;

            NewStage(glstate->list.active, STAGE_DRAW);
            list = glstate->list.active;

            for (int i = 0; i < count; i++)
                sindices[i] -= start;
            list = arrays_to_renderlist(list, mode, start + basevertex, end + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            // end_renderlist(list);

            if (glstate->list.pending) {
                NewStage(glstate->list.active, STAGE_POSTDRAW);
            } else {
                glstate->list.active = extend_renderlist(list);
            }
            return;
        }

        if (intercept) {
            // TODO handling uint indices
            renderlist_t* list = NULL;

            for (int i = 0; i < count; i++)
                sindices[i] -= start;
            list = arrays_to_renderlist(list, mode, start + basevertex, end + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            list = end_renderlist(list);
            draw_renderlist(list);
            free_renderlist(list);

            return;
        } else {
            if (iindices)
                for (int i = 0; i < count; i++)
                    iindices[i] += basevertex;
            else
                for (int i = 0; i < count; i++)
                    sindices[i] += basevertex;
            GLuint old_index = wantBufferIndex(0);
            glDrawElementsCommon(mode, 0, count, end + basevertex + 1, sindices, iindices, 1);
            if (iindices)
                free(iindices);
            else
                free(sindices);
            wantBufferIndex(old_index);
        }
    }
}
AliasExport(void, glDrawRangeElementsBaseVertex, ,
            (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices, GLint basevertex));
AliasExport(void, glDrawRangeElementsBaseVertex, ARB,
            (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices, GLint basevertex));

void APIENTRY_GL4ES gl4es_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount) {
    DBG(SHUT_LOGD("glDrawArraysInstanced(%s, %d, %d, %d), list=%p pending=%d\n", PrintEnum(mode), first, count,
                  primcount, glstate->list.active, glstate->list.pending);)
    count = adjust_vertices(mode, count);

    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }

    // special case for (very) large GL_QUADS array
    if ((mode == GL_QUADS) && (count > 4 * 8000)) {
        // split the array in manageable slice
        int cnt = 4 * 8000;
        for (int i = 0; i < count; i += 4 * 8000) {
            if (i + cnt > count) cnt = count - i;
            gl4es_glDrawArraysInstanced(mode, first + i, cnt, primcount);
        }
        return;
    }
    noerrorShim();

    bool intercept = should_intercept_render(mode);
    // BATCH Mode
    if (!glstate->list.compiling) {
        if ((!intercept && !glstate->list.pending && (count >= MIN_BATCH && count <= MAX_BATCH)) ||
            (intercept && globals4es.maxbatch)) {
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

    if (glstate->list.active) {
        NewStage(glstate->list.active, STAGE_DRAW);
        glstate->list.active = arrays_to_renderlist(glstate->list.active, mode, first, count + first);
        glstate->list.active->instanceCount = primcount;
        if (glstate->list.pending) {
            NewStage(glstate->list.active, STAGE_POSTDRAW);
        } else {
            glstate->list.active = extend_renderlist(glstate->list.active);
        }
        return;
    }

    /*if (glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
        mode = GL_LINE_LOOP;*/
    if (glstate->polygon_mode == GL_POINT && mode >= GL_TRIANGLES) mode = GL_POINTS;

    if (intercept) {
        renderlist_t* list = NULL;
        list = arrays_to_renderlist(list, mode, first, count + first);
        list->instanceCount = primcount;
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    } else {
        if (mode == GL_QUADS) {
            draw_quads_arrays_fast(first, count, primcount);
            return;
        }

        glDrawElementsCommon(mode, first, count, count, NULL, NULL, primcount);
    }
}
AliasExport(void, glDrawArraysInstanced, , (GLenum mode, GLint first, GLsizei count, GLsizei primcount));
AliasExport(void, glDrawArraysInstanced, ARB, (GLenum mode, GLint first, GLsizei count, GLsizei primcount));

void APIENTRY_GL4ES gl4es_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                                  GLsizei primcount) {
    DBG(SHUT_LOGD("glDrawElementsInstanced(%s, %d, %s, %p, %d), list=%p pending=%d\n", PrintEnum(mode), count,
                  PrintEnum(type), indices, primcount, glstate->list.active, glstate->list.pending);)
    count = adjust_vertices(mode, count);

    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }

    bool compiling = (glstate->list.active);
    bool intercept = should_intercept_render(mode);

    // BATCH Mode
    if (!compiling) {
        if ((!intercept && !glstate->list.pending && (count >= MIN_BATCH && count <= MAX_BATCH)) ||
            (intercept && globals4es.maxbatch)) {
            compiling = true;
            glstate->list.pending = 1;
            glstate->list.active = alloc_renderlist();
        }
    }

    noerrorShim();
    GLushort* sindices = NULL;
    GLuint* iindices = NULL;
    GLuint old_index = 0;
    bool need_free =
        !((type == GL_UNSIGNED_SHORT) || (!compiling && !intercept && type == GL_UNSIGNED_INT && hardext.elementuint));
    if (need_free) {
        sindices = copy_gl_array(
            (glstate->vao->elements) ? ((void*)((char*)glstate->vao->elements->data + (uintptr_t)indices)) : indices,
            type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);
        old_index = wantBufferIndex(0);
    } else {
        if (type == GL_UNSIGNED_INT)
            iindices = (glstate->vao->elements) ? ((void*)((char*)glstate->vao->elements->data + (uintptr_t)indices))
                                                : (GLvoid*)indices;
        else
            sindices = (glstate->vao->elements) ? ((void*)((char*)glstate->vao->elements->data + (uintptr_t)indices))
                                                : (GLvoid*)indices;
    }

    if (compiling) {
        // TODO, handle uint indices
        renderlist_t* list = NULL;
        GLsizei min, max;

        NewStage(glstate->list.active, STAGE_DRAW);
        list = glstate->list.active;

        if (!need_free) {
            GLushort* tmp = sindices;
            sindices = (GLushort*)malloc(count * sizeof(GLushort));
            memcpy(sindices, tmp, count * sizeof(GLushort));
        }
        normalize_indices_us(sindices, &max, &min, count);
        list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        list->instanceCount = primcount;
        // end_renderlist(list);

        if (glstate->list.pending) {
            NewStage(glstate->list.active, STAGE_POSTDRAW);
        } else {
            glstate->list.active = extend_renderlist(list);
        }
        return;
    }

    if (intercept) {
        // TODO handling uint indices
        renderlist_t* list = NULL;
        GLsizei min, max;

        if (!need_free) {
            GLushort* tmp = sindices;
            sindices = (GLushort*)malloc(count * sizeof(GLushort));
            memcpy(sindices, tmp, count * sizeof(GLushort));
        }
        normalize_indices_us(sindices, &max, &min, count);
        list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = sindices;
        list->ilen = count;
        list->indice_cap = count;
        list->instanceCount = primcount;
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
        return;
    } else {
        glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, primcount);
        if (need_free) {
            free(sindices);
            wantBufferIndex(old_index);
        }
    }
}
AliasExport(void, glDrawElementsInstanced, ,
            (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount));
AliasExport(void, glDrawElementsInstanced, ARB,
            (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount));

void APIENTRY_GL4ES gl4es_glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                                            const void* indices, GLsizei primcount, GLint basevertex) {
    DBG(SHUT_LOGD("glDrawElementsInstanceBaseVertex(%s, %d, %s, %p, %d, %d), vtx=%p map=%p, pending=%d\n",
                  PrintEnum(mode), count, PrintEnum(type), indices, primcount, basevertex,
                  (glstate->vao->vertex) ? glstate->vao->vertex->data : NULL,
                  (glstate->vao->elements) ? glstate->vao->elements->data : NULL, glstate->list.pending);)
    if (basevertex == 0)
        gl4es_glDrawElementsInstanced(mode, count, type, indices, primcount);
    else {
        count = adjust_vertices(mode, count);

        if (count < 0) {
            errorShim(GL_INVALID_VALUE);
            return;
        }
        if (count == 0) {
            noerrorShim();
            return;
        }

        bool compiling = (glstate->list.active);
        bool intercept = should_intercept_render(mode);

        // BATCH Mode
        if (!compiling) {
            if (!intercept && glstate->list.pending &&
                count > MAX_BATCH) // too large and will not intercept, stop the BATCH
                gl4es_flush();
            else if ((!intercept && !glstate->list.pending && (count >= MIN_BATCH && count <= MAX_BATCH)) ||
                     (intercept && globals4es.maxbatch)) {
                compiling = true;
                glstate->list.pending = 1;
                glstate->list.active = alloc_renderlist();
            }
        }

        noerrorShim();
        GLushort* sindices = NULL;
        GLuint* iindices = NULL;

        if (type == GL_UNSIGNED_INT && hardext.elementuint && !compiling && !intercept)
            iindices = copy_gl_array(
                (glstate->vao->elements) ? (void*)((char*)glstate->vao->elements->data + (uintptr_t)indices) : indices,
                type, 1, 0, GL_UNSIGNED_INT, 1, 0, count, NULL);
        else
            sindices = copy_gl_array(
                (glstate->vao->elements) ? (void*)((char*)glstate->vao->elements->data + (uintptr_t)indices) : indices,
                type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count, NULL);

        if (compiling) {
            // TODO, handle uint indices
            renderlist_t* list = NULL;
            GLsizei min, max;

            NewStage(glstate->list.active, STAGE_DRAW);
            list = glstate->list.active;

            normalize_indices_us(sindices, &max, &min, count);
            list = arrays_to_renderlist(list, mode, min + basevertex, max + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            list->instanceCount = primcount;
            // end_renderlist(list);

            if (glstate->list.pending) {
                NewStage(glstate->list.active, STAGE_POSTDRAW);
            } else {
                glstate->list.active = extend_renderlist(list);
            }
            return;
        }

        if (intercept) {
            // TODO handling uint indices
            renderlist_t* list = NULL;
            GLsizei min, max;

            normalize_indices_us(sindices, &max, &min, count);
            list = arrays_to_renderlist(list, mode, min + basevertex, max + basevertex + 1);
            list->indices = sindices;
            list->ilen = count;
            list->indice_cap = count;
            list->instanceCount = primcount;
            list = end_renderlist(list);
            draw_renderlist(list);
            free_renderlist(list);
            return;
        } else {
            if (iindices)
                for (int i = 0; i < count; i++)
                    iindices[i] += basevertex;
            else
                for (int i = 0; i < count; i++)
                    sindices[i] += basevertex;
            GLuint old_index = wantBufferIndex(0);
            glDrawElementsCommon(mode, 0, count, 0, sindices, iindices, primcount);
            if (iindices)
                free(iindices);
            else
                free(sindices);
            wantBufferIndex(old_index);
        }
    }
}
AliasExport(void, glDrawElementsInstancedBaseVertex, ,
            (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount, GLint basevertex));
AliasExport(void, glDrawElementsInstancedBaseVertex, ARB,
            (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount, GLint basevertex));
AliasExport(void, glDrawElementsInstancedBaseVertex, EXT,
            (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount, GLint basevertex));
AliasExport(void, glDrawArraysInstanced, EXT, (GLenum mode, GLint first, GLsizei count, GLsizei primcount));
AliasExport(void, glDrawElementsInstanced, EXT,
            (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount));
AliasExport(void, glDrawElementsBaseVertex, EXT,
            (GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex));
AliasExport(void, glDrawRangeElementsBaseVertex, EXT,
            (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices,
             GLint basevertex));
