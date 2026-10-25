#include "uniform.h"

#include "../glx/hardext.h"
#include "gl4es.h"
#include "glstate.h"
#include "loader.h"
#include "matvec.h"
#include "shadermod_uniforms.h"
#include "GL/gl.h"

// #define DEBUG
#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

typedef void (APIENTRY_GLES *emesis_glUniform_uiv_pfn)(GLint location, GLsizei count, const GLuint *value);
typedef void (APIENTRY_GLES *glUniformMatrix2x3fv_PTR)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY_GLES *glUniformMatrix3x2fv_PTR)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY_GLES *glUniformMatrix2x4fv_PTR)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY_GLES *glUniformMatrix4x2fv_PTR)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY_GLES *glUniformMatrix3x4fv_PTR)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY_GLES *glUniformMatrix4x3fv_PTR)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY_GLES *glGetUniformIndices_PTR)(GLuint, GLsizei, const GLchar* const*, GLuint*);
typedef void (APIENTRY_GLES *glGetActiveUniformsiv_PTR)(GLuint, GLsizei, const GLuint*, GLenum, GLint*);
static emesis_glUniform_uiv_pfn p_gles_glUniform1uiv = NULL;
static emesis_glUniform_uiv_pfn p_gles_glUniform2uiv = NULL;
static emesis_glUniform_uiv_pfn p_gles_glUniform3uiv = NULL;
static emesis_glUniform_uiv_pfn p_gles_glUniform4uiv = NULL;
static int g_loaded_uiv_pfns = 0;

static void ensure_uiv_pfns(void) {
    if (!g_loaded_uiv_pfns && gles) {
        p_gles_glUniform1uiv = (emesis_glUniform_uiv_pfn)proc_address(gles, "glUniform1uiv");
        p_gles_glUniform2uiv = (emesis_glUniform_uiv_pfn)proc_address(gles, "glUniform2uiv");
        p_gles_glUniform3uiv = (emesis_glUniform_uiv_pfn)proc_address(gles, "glUniform3uiv");
        p_gles_glUniform4uiv = (emesis_glUniform_uiv_pfn)proc_address(gles, "glUniform4uiv");
        g_loaded_uiv_pfns = 1;
    }
}

static int is_sampler_or_image_uniform(GLenum type) {
    return (type >= 0x8B5D && type <= 0x8B64) || /* GL_SAMPLER_1D..GL_SAMPLER_2D_RECT_SHADOW */
           (type >= 0x8DC0 && type <= 0x8DC5) || /* GL_SAMPLER_1D_ARRAY..GL_SAMPLER_CUBE_SHADOW */
           (type >= 0x8DC9 && type <= 0x8DDD) || /* GL_INT_SAMPLER_* & GL_UNSIGNED_INT_SAMPLER_* */
           (type >= 0x900C && type <= 0x900F) || /* GL_SAMPLER_CUBE_MAP_ARRAY* */
           (type >= 0x904C && type <= 0x906C) || /* GL_IMAGE_*, GL_INT_IMAGE_*, GL_UNSIGNED_INT_IMAGE_* */
           (type >= 0x9108 && type <= 0x910D);   /* GL_SAMPLER_2D_MULTISAMPLE* */
}

int uniformsize(GLenum type) {
    if (is_sampler_or_image_uniform(type)) {
        return sizeof(GLint);
    }
#define GO(T, t, s)                                                                                                    \
    case T:                                                                                                            \
        return sizeof(t) * s
#define GOV(T, t)                                                                                                      \
    GO(T, t, 1);                                                                                                       \
    GO(T##_VEC2, t, 2);                                                                                                \
    GO(T##_VEC3, t, 3);                                                                                                \
    GO(T##_VEC4, t, 4)
#define GOM(T, t)                                                                                                      \
    GO(T##_MAT2, t, 2 * 2);                                                                                            \
    GO(T##_MAT3, t, 3 * 3);                                                                                            \
    GO(T##_MAT4, t, 4 * 4);
    switch (type) {
        // GLES2 & GLES3 types
        GOV(GL_FLOAT, GLfloat);
        GOV(GL_INT, GLint);
        GOV(GL_UNSIGNED_INT, GLuint);
        GOV(GL_BOOL, /*GLboolean*/ GLint); // GLboolean is an unsigned char and is not suitable here
        GOM(GL_FLOAT, GLfloat);
        GO(0x8B65 /* GL_FLOAT_MAT2x3 */, GLfloat, 2 * 3);
        GO(0x8B66 /* GL_FLOAT_MAT2x4 */, GLfloat, 2 * 4);
        GO(0x8B67 /* GL_FLOAT_MAT3x2 */, GLfloat, 3 * 2);
        GO(0x8B68 /* GL_FLOAT_MAT3x4 */, GLfloat, 3 * 4);
        GO(0x8B69 /* GL_FLOAT_MAT4x2 */, GLfloat, 4 * 2);
        GO(0x8B6A /* GL_FLOAT_MAT4x3 */, GLfloat, 4 * 3);
    }
    return 0;
#undef GOM
#undef GOV
#undef GO
}

int is_uniform_float(GLenum type) {
    switch (type) {
    case GL_FLOAT:
    case GL_FLOAT_VEC2:
    case GL_FLOAT_VEC3:
    case GL_FLOAT_VEC4:
    case GL_FLOAT_MAT2:
    case GL_FLOAT_MAT3:
    case GL_FLOAT_MAT4:
    case 0x8B65:
    case 0x8B66:
    case 0x8B67:
    case 0x8B68:
    case 0x8B69:
    case 0x8B6A:
        return 1;
    }
    return 0;
}
int is_uniform_uint(GLenum type) {
    switch (type) {
    case GL_UNSIGNED_INT:
    case GL_UNSIGNED_INT_VEC2:
    case GL_UNSIGNED_INT_VEC3:
    case GL_UNSIGNED_INT_VEC4:
        return 1;
    }
    return 0;
}
int is_uniform_int(GLenum type) {
    if (is_sampler_or_image_uniform(type) || is_uniform_uint(type)) {
        return 1;
    }
    switch (type) {
    case GL_INT:
    case GL_INT_VEC2:
    case GL_INT_VEC3:
    case GL_INT_VEC4:
    case GL_BOOL:
    case GL_BOOL_VEC2:
    case GL_BOOL_VEC3:
    case GL_BOOL_VEC4:
        return 1;
    }
    return 0;
}
int is_uniform_matrix(GLenum type) {
    switch (type) {
    case GL_FLOAT_MAT2:
    case GL_FLOAT_MAT3:
    case GL_FLOAT_MAT4:
    case 0x8B65:
    case 0x8B66:
    case 0x8B67:
    case 0x8B68:
    case 0x8B69:
    case 0x8B6A:
        return 1;
    }
    return 0;
}

int n_uniform(GLenum type) {
    if (is_sampler_or_image_uniform(type)) {
        return 1;
    }
    // matrix type will return 0
    switch (type) {
    case GL_FLOAT:
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_BOOL:
        return 1;
    case GL_FLOAT_VEC2:
    case GL_INT_VEC2:
    case GL_UNSIGNED_INT_VEC2:
    case GL_BOOL_VEC2:
        return 2;
    case GL_FLOAT_VEC3:
    case GL_INT_VEC3:
    case GL_UNSIGNED_INT_VEC3:
    case GL_BOOL_VEC3:
        return 3;
    case GL_FLOAT_VEC4:
    case GL_INT_VEC4:
    case GL_UNSIGNED_INT_VEC4:
    case GL_BOOL_VEC4:
        return 4;
    }
    return 0;
}

void APIENTRY_GL4ES gl4es_glGetUniformfv(GLuint program, GLint location, GLfloat* params) {
    DBG(SHUT_LOGD("glGetUniformfv(%d, %d, %p)\n", program, location, params);)
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);

    khint_t k;
    uniform_t* gluniform = NULL;
    k = kh_get(uniformlist, glprogram->uniform, location);
    if (k != kh_end(glprogram->uniform)) {
        gluniform = kh_value(glprogram->uniform, k);
        uintptr_t offs = gluniform->cache_offs;
        int size = gluniform->cache_size;
        if (is_uniform_float(gluniform->type)) {
            memcpy(params, (char*)glprogram->cache.cache + offs, size);
            noerrorShim();
            return;
        }
        // if it's not float, it can be only int for now
        int n = size / sizeof(GLint);
        GLint* fl = (GLint*)((char*)glprogram->cache.cache + offs);
        for (int i = 0; i < n; i++)
            params[i] = fl[i]; // this is probably good, int->float is straight forward
        noerrorShim();
        return;
    }
    errorShim(GL_INVALID_VALUE);
}
void APIENTRY_GL4ES gl4es_glGetUniformiv(GLuint program, GLint location, GLint* params) {
    DBG(SHUT_LOGD("glGetUniformiv(%d, %d, %p)\n", program, location, params);)
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);

    khint_t k;
    uniform_t* gluniform = NULL;
    k = kh_get(uniformlist, glprogram->uniform, location);
    if (k != kh_end(glprogram->uniform)) {
        gluniform = kh_value(glprogram->uniform, k);
        uintptr_t offs = gluniform->cache_offs;
        int size = gluniform->cache_size;
        if (is_uniform_int(gluniform->type)) {
            memcpy(params, (char*)glprogram->cache.cache + offs, size);
            noerrorShim();
            return;
        }
        // if it's not int, it can be only float for now
        int n = size / sizeof(GLfloat);
        GLfloat* fl = (GLfloat*)((char*)glprogram->cache.cache + offs);
        for (int i = 0; i < n; i++)
            params[i] = fl[i]; // is this correct? float -> int without adjustment?
        noerrorShim();
        return;
    }
    errorShim(GL_INVALID_VALUE);
}
void APIENTRY_GL4ES gl4es_glGetUniformuiv(GLuint program, GLint location, GLuint* params) {
    DBG(SHUT_LOGD("glGetUniformuiv(%d, %d, %p)\n", program, location, params);)
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);

    khint_t k = kh_get(uniformlist, glprogram->uniform, location);
    if (k != kh_end(glprogram->uniform)) {
        uniform_t* gluniform = kh_value(glprogram->uniform, k);
        uintptr_t offs = gluniform->cache_offs;
        int size = gluniform->cache_size;
        if (is_uniform_int(gluniform->type)) {
            memcpy(params, (char*)glprogram->cache.cache + offs, size);
            noerrorShim();
            return;
        }
        int n = size / sizeof(GLfloat);
        GLfloat* fl = (GLfloat*)((char*)glprogram->cache.cache + offs);
        for (int i = 0; i < n; i++)
            params[i] = (GLuint)fl[i];
        noerrorShim();
        return;
    }
    errorShim(GL_INVALID_VALUE);
}

static void queue_pending_uniform(program_t* glprogram, uniform_t* m, int count) {
    if (!glprogram || !m || count <= 0) return;
    if (count > m->size) count = m->size;
    if ((uint16_t)count > m->pending_elems) {
        m->pending_elems = (uint16_t)count;
    }
    if (!m->pending_upload) {
        if (glprogram->pending_count >= glprogram->pending_cap) {
            int new_cap = glprogram->pending_cap ? (glprogram->pending_cap * 2) : 32;
            uniform_t** nl = (uniform_t**)realloc(glprogram->pending_list, (size_t)new_cap * sizeof(uniform_t*));
            if (!nl) return;
            glprogram->pending_list = nl;
            glprogram->pending_cap = new_cap;
        }
        glprogram->pending_list[glprogram->pending_count++] = m;
        m->pending_upload = 1;
    }
}

void flush_pending_uniforms(program_t* glprogram) {
    if (!glprogram || glprogram->pending_count <= 0 || !glprogram->pending_list || !glprogram->cache.cache) return;
    LOAD_GLES2(glUniform1fv);
    LOAD_GLES2(glUniform2fv);
    LOAD_GLES2(glUniform3fv);
    LOAD_GLES2(glUniform4fv);
    LOAD_GLES2(glUniform1iv);
    LOAD_GLES2(glUniform2iv);
    LOAD_GLES2(glUniform3iv);
    LOAD_GLES2(glUniform4iv);
    LOAD_GLES2(glUniformMatrix2fv);
    LOAD_GLES2(glUniformMatrix3fv);
    LOAD_GLES2(glUniformMatrix4fv);

    int total = glprogram->pending_count;
    glprogram->pending_count = 0;

    for (int i = 0; i < total; ++i) {
        uniform_t* m = glprogram->pending_list[i];
        if (!m || !m->pending_upload) continue;
        int count = (int)m->pending_elems;
        if (count <= 0) count = 1;
        if (count > m->size) count = m->size;
        m->pending_upload = 0;
        m->pending_elems = 0;

        const void* ptr = (const void*)((const char*)glprogram->cache.cache + m->cache_offs);
        if (m->type == GL_FLOAT_MAT2) {
            if (gles_glUniformMatrix2fv) gles_glUniformMatrix2fv(m->id, count, GL_FALSE, (const GLfloat*)ptr);
        } else if (m->type == GL_FLOAT_MAT3) {
            if (gles_glUniformMatrix3fv) gles_glUniformMatrix3fv(m->id, count, GL_FALSE, (const GLfloat*)ptr);
        } else if (m->type == GL_FLOAT_MAT4) {
            if (gles_glUniformMatrix4fv) gles_glUniformMatrix4fv(m->id, count, GL_FALSE, (const GLfloat*)ptr);
        } else if (m->type >= 0x8B65 && m->type <= 0x8B6A) {
            switch (m->type) {
            case 0x8B65: {
                LOAD_GLES3(glUniformMatrix2x3fv);
                if (gles_glUniformMatrix2x3fv) gles_glUniformMatrix2x3fv(m->id, count, GL_FALSE, (const GLfloat*)ptr);
                break;
            }
            case 0x8B66: {
                LOAD_GLES3(glUniformMatrix2x4fv);
                if (gles_glUniformMatrix2x4fv) gles_glUniformMatrix2x4fv(m->id, count, GL_FALSE, (const GLfloat*)ptr);
                break;
            }
            case 0x8B67: {
                LOAD_GLES3(glUniformMatrix3x2fv);
                if (gles_glUniformMatrix3x2fv) gles_glUniformMatrix3x2fv(m->id, count, GL_FALSE, (const GLfloat*)ptr);
                break;
            }
            case 0x8B68: {
                LOAD_GLES3(glUniformMatrix3x4fv);
                if (gles_glUniformMatrix3x4fv) gles_glUniformMatrix3x4fv(m->id, count, GL_FALSE, (const GLfloat*)ptr);
                break;
            }
            case 0x8B69: {
                LOAD_GLES3(glUniformMatrix4x2fv);
                if (gles_glUniformMatrix4x2fv) gles_glUniformMatrix4x2fv(m->id, count, GL_FALSE, (const GLfloat*)ptr);
                break;
            }
            case 0x8B6A: {
                LOAD_GLES3(glUniformMatrix4x3fv);
                if (gles_glUniformMatrix4x3fv) gles_glUniformMatrix4x3fv(m->id, count, GL_FALSE, (const GLfloat*)ptr);
                break;
            }
            }
        } else if (is_uniform_float(m->type)) {
            int sz = n_uniform(m->type);
            if (gles_glUniform1fv) {
                switch (sz) {
                case 1: gles_glUniform1fv(m->id, count, (const GLfloat*)ptr); break;
                case 2: gles_glUniform2fv(m->id, count, (const GLfloat*)ptr); break;
                case 3: gles_glUniform3fv(m->id, count, (const GLfloat*)ptr); break;
                case 4: gles_glUniform4fv(m->id, count, (const GLfloat*)ptr); break;
                }
            }
        } else if (is_uniform_uint(m->type)) {
            int sz = n_uniform(m->type);
            ensure_uiv_pfns();
            if (p_gles_glUniform1uiv) {
                switch (sz) {
                case 1: p_gles_glUniform1uiv(m->id, count, (const GLuint*)ptr); break;
                case 2: if (p_gles_glUniform2uiv) p_gles_glUniform2uiv(m->id, count, (const GLuint*)ptr); break;
                case 3: if (p_gles_glUniform3uiv) p_gles_glUniform3uiv(m->id, count, (const GLuint*)ptr); break;
                case 4: if (p_gles_glUniform4uiv) p_gles_glUniform4uiv(m->id, count, (const GLuint*)ptr); break;
                }
            }
        } else if (is_uniform_int(m->type)) {
            int sz = n_uniform(m->type);
            if (gles_glUniform1iv) {
                switch (sz) {
                case 1: gles_glUniform1iv(m->id, count, (const GLint*)ptr); break;
                case 2: gles_glUniform2iv(m->id, count, (const GLint*)ptr); break;
                case 3: gles_glUniform3iv(m->id, count, (const GLint*)ptr); break;
                case 4: gles_glUniform4iv(m->id, count, (const GLint*)ptr); break;
                }
            }
        }
    }
}

void GoUniformfv(program_t* glprogram, GLint location, int size, int count, const GLfloat* value) {
    DBG(SHUT_LOGD("GoUniformfv(%p[%d], %d, %d, %d, %p) =>(%f...)\n", glprogram, glprogram->id, location, size, count,
                  value, value[0]);)
    if (location == -1) {
        noerrorShim();
        return;
    }
    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }

    khint_t k;
    uniform_t* m;
    k = kh_get(uniformlist, glprogram->uniform, location);
    if (k == kh_end(glprogram->uniform)) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    m = kh_value(glprogram->uniform, k);
    if (!is_uniform_float(m->type)) {
        if (!is_sampler_or_image_uniform(m->type) && is_uniform_int(m->type) &&
            size == n_uniform(m->type) && count <= m->size) {
            if (count == 0) {
                noerrorShim();
                return;
            }
            int total = size * count;
            GLint stack_i[64];
            GLint* ibuf = (total <= 64) ? stack_i : (GLint*)malloc((size_t)total * sizeof(GLint));
            if (!ibuf) {
                errorShim(GL_OUT_OF_MEMORY);
                return;
            }
            int is_bool = (m->type == GL_BOOL || m->type == GL_BOOL_VEC2 ||
                           m->type == GL_BOOL_VEC3 || m->type == GL_BOOL_VEC4);
            for (int idx = 0; idx < total; ++idx) {
                ibuf[idx] = is_bool ? ((value[idx] != 0.0f) ? 1 : 0) : (GLint)value[idx];
            }
            GoUniformiv(glprogram, location, size, count, ibuf);
            if (ibuf != stack_i) free(ibuf);
            return;
        }
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (size != n_uniform(m->type) || count > m->size) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }
    if (!g_emesis_internal_uniform_upload) {
        m->app_set = 1;
        if (m->sm_source_id) {
            emesis_sm_observe_float(m->sm_source_id, size, count, value);
        }
    }
    if (m->twin_loc != -1) {
        GoUniformfv(glprogram, m->twin_loc, size, count, value);
    }
    // ok, check the value in the cache BEFORE switching active GLES program
    int rsize = sizeof(GLfloat) * size * count;
    if (memcmp((char*)glprogram->cache.cache + m->cache_offs, value, rsize) == 0) {
        noerrorShim();
        return; // nothing to do, same value already there
    }
    // update uniform
    memcpy((char*)glprogram->cache.cache + m->cache_offs, value, rsize);
    m->last_mod_gen = ++glprogram->uniform_gen;
    if (glstate->gleshard->program != glprogram->id && !glprogram->last_comp) {
        queue_pending_uniform(glprogram, m, count);
        noerrorShim();
        return;
    }
    APPLY_PROGRAM(glprogram->id, glprogram);
    if (m->pending_upload && count >= (int)m->pending_elems) {
        m->pending_upload = 0;
        m->pending_elems = 0;
    }
    LOAD_GLES2(glUniform1fv);
    LOAD_GLES2(glUniform2fv);
    LOAD_GLES2(glUniform3fv);
    LOAD_GLES2(glUniform4fv);
    if (gles_glUniform1fv) {
        switch (size) {
        case 1:
            gles_glUniform1fv(m->id, count, value);
            break;
        case 2:
            gles_glUniform2fv(m->id, count, value);
            break;
        case 3:
            gles_glUniform3fv(m->id, count, value);
            break;
        case 4:
            gles_glUniform4fv(m->id, count, value);
            break;
        }
        errorGL();
    } else
        errorShim(GL_INVALID_OPERATION); // no GLSL hardware
}

void GoUniformuiv(program_t* glprogram, GLint location, int size, int count, const GLuint* value) {
    if (location == -1) {
        noerrorShim();
        return;
    }
    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }

    khint_t k = kh_get(uniformlist, glprogram->uniform, location);
    if (k == kh_end(glprogram->uniform)) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    uniform_t* m = kh_value(glprogram->uniform, k);
    if (size != n_uniform(m->type) || !is_uniform_uint(m->type) || count > m->size) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }
    if (!g_emesis_internal_uniform_upload) {
        m->app_set = 1;
    }
    int rsize = sizeof(GLuint) * size * count;
    if (memcmp((char*)glprogram->cache.cache + m->cache_offs, value, rsize) == 0) {
        noerrorShim();
        return;
    }
    memcpy((char*)glprogram->cache.cache + m->cache_offs, value, rsize);
    m->last_mod_gen = ++glprogram->uniform_gen;
    if (glstate->gleshard->program != glprogram->id && !glprogram->last_comp) {
        queue_pending_uniform(glprogram, m, count);
        noerrorShim();
        return;
    }
    APPLY_PROGRAM(glprogram->id, glprogram);
    if (m->pending_upload && count >= (int)m->pending_elems) {
        m->pending_upload = 0;
        m->pending_elems = 0;
    }
    ensure_uiv_pfns();
    if (p_gles_glUniform1uiv) {
        switch (size) {
        case 1:
            p_gles_glUniform1uiv(m->id, count, value);
            break;
        case 2:
            if (p_gles_glUniform2uiv) p_gles_glUniform2uiv(m->id, count, value);
            break;
        case 3:
            if (p_gles_glUniform3uiv) p_gles_glUniform3uiv(m->id, count, value);
            break;
        case 4:
            if (p_gles_glUniform4uiv) p_gles_glUniform4uiv(m->id, count, value);
            break;
        }
        errorGL();
    } else {
        errorShim(GL_INVALID_OPERATION);
    }
}

void GoUniformiv(program_t* glprogram, GLint location, int size, int count, const GLint* value) {
    DBG(SHUT_LOGD("GoUniformiv(%p[%d], %d, %d, %d, %p) =>(%d...)\n", glprogram, glprogram->id, location, size, count,
                  value, value[0]);)
    if (location == -1) {
        noerrorShim();
        return;
    }
    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }

    khint_t k;
    uniform_t* m;
    k = kh_get(uniformlist, glprogram->uniform, location);
    if (k == kh_end(glprogram->uniform)) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    m = kh_value(glprogram->uniform, k);
    if (is_uniform_uint(m->type)) {
        GoUniformuiv(glprogram, location, size, count, (const GLuint*)value);
        return;
    }
    if (!is_uniform_int(m->type)) {
        if ((m->type == GL_FLOAT || m->type == GL_FLOAT_VEC2 ||
             m->type == GL_FLOAT_VEC3 || m->type == GL_FLOAT_VEC4) &&
            size == n_uniform(m->type) && count <= m->size) {
            if (count == 0) {
                noerrorShim();
                return;
            }
            int total = size * count;
            GLfloat stack_f[64];
            GLfloat* fbuf = (total <= 64) ? stack_f : (GLfloat*)malloc((size_t)total * sizeof(GLfloat));
            if (!fbuf) {
                errorShim(GL_OUT_OF_MEMORY);
                return;
            }
            for (int idx = 0; idx < total; ++idx) {
                fbuf[idx] = (GLfloat)value[idx];
            }
            GoUniformfv(glprogram, location, size, count, fbuf);
            if (fbuf != stack_f) free(fbuf);
            return;
        }
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (size != n_uniform(m->type) || count > m->size) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }
    if (!g_emesis_internal_uniform_upload) {
        m->app_set = 1;
        if (m->sm_source_id) {
            emesis_sm_observe_int(m->sm_source_id, size, count, value);
        }
    }
    if (m->twin_loc != -1) {
        GoUniformiv(glprogram, m->twin_loc, size, count, value);
    }
    // ok, check the value in the cache BEFORE switching active GLES program
    int rsize = sizeof(GLint) * size * count;
    if (memcmp((char*)glprogram->cache.cache + m->cache_offs, value, rsize) == 0) {
        noerrorShim();
        return; // nothing to do, same value already there
    }
    DBG(SHUT_LOGD("Uniform updated, cache=%p(%d/%d), offset=%p, size=%d\n", glprogram->cache.cache,
                  glprogram->cache.size, glprogram->cache.cap, (void*)m->cache_offs, rsize);)
    // update uniform
    memcpy((char*)glprogram->cache.cache + m->cache_offs, value, rsize);
    m->last_mod_gen = ++glprogram->uniform_gen;
    if (glstate->gleshard->program != glprogram->id && !glprogram->last_comp) {
        queue_pending_uniform(glprogram, m, count);
        noerrorShim();
        return;
    }
    APPLY_PROGRAM(glprogram->id, glprogram);
    if (m->pending_upload && count >= (int)m->pending_elems) {
        m->pending_upload = 0;
        m->pending_elems = 0;
    }
    LOAD_GLES2(glUniform1iv);
    LOAD_GLES2(glUniform2iv);
    LOAD_GLES2(glUniform3iv);
    LOAD_GLES2(glUniform4iv);
    if (gles_glUniform1iv) {
        switch (size) {
        case 1:
            gles_glUniform1iv(m->id, count, value);
            break;
        case 2:
            gles_glUniform2iv(m->id, count, value);
            break;
        case 3:
            gles_glUniform3iv(m->id, count, value);
            break;
        case 4:
            gles_glUniform4iv(m->id, count, value);
            break;
        }
        errorGL();
    } else
        errorShim(GL_INVALID_OPERATION); // no GLSL hardware
}

void APIENTRY_GL4ES gl4es_glUniform1f(GLint location, GLfloat v0) {
    DBG(SHUT_LOGD("glUniform1f(%d, %f)\n", location, v0);)
    PUSH_IF_COMPILING(glUniform1f);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 1, 1, &v0);
}
void APIENTRY_GL4ES gl4es_glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    DBG(SHUT_LOGD("glUniform2f(%d, %f, %f)\n", location, v0, v1);)
    PUSH_IF_COMPILING(glUniform2f);
    GLfloat fl[2] = {v0, v1};
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 2, 1, fl);
}
void APIENTRY_GL4ES gl4es_glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    DBG(SHUT_LOGD("glUniform3f(%d, %f, %f, %f)\n", location, v0, v1, v2);)
    PUSH_IF_COMPILING(glUniform3f);
    GLfloat fl[3] = {v0, v1, v2};
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 3, 1, fl);
}
void APIENTRY_GL4ES gl4es_glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    DBG(SHUT_LOGD("glUniform4f(%d, %f, %f, %f, %f)\n", location, v0, v1, v2, v3);)
    PUSH_IF_COMPILING(glUniform4f);
    GLfloat fl[4] = {v0, v1, v2, v3};
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 4, 1, fl);
}
void APIENTRY_GL4ES gl4es_glUniform1i(GLint location, GLint v0) {
    DBG(SHUT_LOGD("glUniform1i(%d, %d)\n", location, v0);)
    PUSH_IF_COMPILING(glUniform1i);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 1, 1, &v0);
}
void APIENTRY_GL4ES gl4es_glUniform2i(GLint location, GLint v0, GLint v1) {
    DBG(SHUT_LOGD("glUniform2i(%d, %d, %d)\n", location, v0, v1);)
    PUSH_IF_COMPILING(glUniform2i);
    GLint fl[2] = {v0, v1};
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 2, 1, fl);
}
void APIENTRY_GL4ES gl4es_glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    DBG(SHUT_LOGD("glUniform3i(%d, %d, %d, %d)\n", location, v0, v1, v2);)
    PUSH_IF_COMPILING(glUniform3i);
    GLint fl[3] = {v0, v1, v2};
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 3, 1, fl);
}
void APIENTRY_GL4ES gl4es_glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    DBG(SHUT_LOGD("glUniform4i(%d, %d, %d, %d, %d)\n", location, v0, v1, v2, v3);)
    PUSH_IF_COMPILING(glUniform4i);
    GLint fl[4] = {v0, v1, v2, v3};
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 4, 1, fl);
}
void APIENTRY_GL4ES gl4es_glUniform1ui(GLint location, GLuint v0) {
    FLUSH_BEGINEND;
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 1, 1, &v0);
}
void APIENTRY_GL4ES gl4es_glUniform2ui(GLint location, GLuint v0, GLuint v1) {
    FLUSH_BEGINEND;
    GLuint fl[2] = {v0, v1};
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 2, 1, fl);
}
void APIENTRY_GL4ES gl4es_glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
    FLUSH_BEGINEND;
    GLuint fl[3] = {v0, v1, v2};
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 3, 1, fl);
}
void APIENTRY_GL4ES gl4es_glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
    FLUSH_BEGINEND;
    GLuint fl[4] = {v0, v1, v2, v3};
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 4, 1, fl);
}
// TODO: the "v" variant and matrix variant cannot be pushed simply...
void APIENTRY_GL4ES gl4es_glUniform1fv(GLint location, GLsizei count, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniform1fv(%d, %d, %p) =>(%f)\n", location, count, value, value[0]);)
    PUSH_IF_COMPILING(glUniform1fv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 1, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform2fv(GLint location, GLsizei count, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniform2fv(%d, %d, %p) =>(%f %f)\n", location, count, value, value[0], value[1]);)
    PUSH_IF_COMPILING(glUniform2fv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 2, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniform3fv(%d, %d, %p) =>(%f %f, %f)\n", location, count, value, value[0], value[1], value[2]);)
    PUSH_IF_COMPILING(glUniform3fv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 3, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniform4fv(%d, %d, %p) =>(%f %f, %f, %f)\n", location, count, value, value[0], value[1], value[2],
                  value[3]);)
    PUSH_IF_COMPILING(glUniform4fv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 4, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform1iv(GLint location, GLsizei count, const GLint* value) {
    PUSH_IF_COMPILING(glUniform1iv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 1, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform2iv(GLint location, GLsizei count, const GLint* value) {
    PUSH_IF_COMPILING(glUniform2iv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 2, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform3iv(GLint location, GLsizei count, const GLint* value) {
    PUSH_IF_COMPILING(glUniform3iv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 3, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform4iv(GLint location, GLsizei count, const GLint* value) {
    PUSH_IF_COMPILING(glUniform4iv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 4, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform1uiv(GLint location, GLsizei count, const GLuint* value) {
    FLUSH_BEGINEND;
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 1, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform2uiv(GLint location, GLsizei count, const GLuint* value) {
    FLUSH_BEGINEND;
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 2, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform3uiv(GLint location, GLsizei count, const GLuint* value) {
    FLUSH_BEGINEND;
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 3, count, value);
}
void APIENTRY_GL4ES gl4es_glUniform4uiv(GLint location, GLsizei count, const GLuint* value) {
    FLUSH_BEGINEND;
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 4, count, value);
}

void APIENTRY_GL4ES gl4es_glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniformMatrix2fv(%d, %d, %d, %p)\n", location, count, transpose, value);)
    PUSH_IF_COMPILING(glUniformMatrix2fv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformMatrix2fv(glprogram, location, count, transpose, value);
}

void APIENTRY_GL4ES gl4es_glProgramUniform1f(GLuint program, GLint location, GLfloat v0) {
    DBG(SHUT_LOGD("glUniform1f(%d, %f)\n", location, v0);)
    PUSH_IF_COMPILING(glUniform1f);
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 1, 1, &v0);
}
void APIENTRY_GL4ES gl4es_glProgramUniform2f(GLuint program, GLint location, GLfloat v0, GLfloat v1) {
    DBG(SHUT_LOGD("glUniform2f(%d, %f, %f)\n", location, v0, v1);)
    PUSH_IF_COMPILING(glUniform2f);
    GLfloat fl[2] = {v0, v1};
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 2, 1, fl);
}
void APIENTRY_GL4ES gl4es_glProgramUniform3f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    DBG(SHUT_LOGD("glUniform3f(%d, %f, %f, %f)\n", location, v0, v1, v2);)
    PUSH_IF_COMPILING(glUniform3f);
    GLfloat fl[3] = {v0, v1, v2};
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 3, 1, fl);
}
void APIENTRY_GL4ES gl4es_glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                                             GLfloat v3) {
    DBG(SHUT_LOGD("glUniform4f(%d, %f, %f, %f, %f)\n", location, v0, v1, v2, v3);)
    PUSH_IF_COMPILING(glUniform4f);
    GLfloat fl[4] = {v0, v1, v2, v3};
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 4, 1, fl);
}
void APIENTRY_GL4ES gl4es_glProgramUniform1i(GLuint program, GLint location, GLint v0) {
    DBG(SHUT_LOGD("glUniform1i(%d, %d)\n", location, v0);)
    PUSH_IF_COMPILING(glUniform1i);
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 1, 1, &v0);
}
void APIENTRY_GL4ES gl4es_glProgramUniform2i(GLuint program, GLint location, GLint v0, GLint v1) {
    DBG(SHUT_LOGD("glUniform2i(%d, %d, %d)\n", location, v0, v1);)
    PUSH_IF_COMPILING(glUniform2i);
    GLint fl[2] = {v0, v1};
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 2, 1, fl);
}
void APIENTRY_GL4ES gl4es_glProgramUniform3i(GLuint program, GLint location, GLint v0, GLint v1, GLint v2) {
    DBG(SHUT_LOGD("glUniform3i(%d, %d, %d, %d)\n", location, v0, v1, v2);)
    PUSH_IF_COMPILING(glUniform3i);
    GLint fl[3] = {v0, v1, v2};
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 3, 1, fl);
}
void APIENTRY_GL4ES gl4es_glProgramUniform4i(GLuint program, GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    DBG(SHUT_LOGD("glUniform4i(%d, %d, %d, %d, %d)\n", location, v0, v1, v2, v3);)
    PUSH_IF_COMPILING(glUniform4i);
    GLint fl[4] = {v0, v1, v2, v3};
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 4, 1, fl);
}
void APIENTRY_GL4ES gl4es_glProgramUniform1ui(GLuint program, GLint location, GLuint v0) {
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 1, 1, &v0);
}
void APIENTRY_GL4ES gl4es_glProgramUniform2ui(GLuint program, GLint location, GLuint v0, GLuint v1) {
    FLUSH_BEGINEND;
    GLuint fl[2] = {v0, v1};
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 2, 1, fl);
}
void APIENTRY_GL4ES gl4es_glProgramUniform3ui(GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2) {
    FLUSH_BEGINEND;
    GLuint fl[3] = {v0, v1, v2};
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 3, 1, fl);
}
void APIENTRY_GL4ES gl4es_glProgramUniform4ui(GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2,
                                              GLuint v3) {
    FLUSH_BEGINEND;
    GLuint fl[4] = {v0, v1, v2, v3};
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 4, 1, fl);
}
// TODO: the "v" variant and matrix variant cannot be pushed simply...
void APIENTRY_GL4ES gl4es_glProgramUniform1fv(GLuint program, GLint location, GLsizei count, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniform1fv(%d, %d, %p) =>(%f)\n", location, count, value, value[0]);)
    PUSH_IF_COMPILING(glUniform1fv);
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 1, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform2fv(GLuint program, GLint location, GLsizei count, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniform2fv(%d, %d, %p) =>(%f %f)\n", location, count, value, value[0], value[1]);)
    PUSH_IF_COMPILING(glUniform2fv);
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 2, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform3fv(GLuint program, GLint location, GLsizei count, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniform3fv(%d, %d, %p) =>(%f %f, %f)\n", location, count, value, value[0], value[1], value[2]);)
    PUSH_IF_COMPILING(glUniform3fv);
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 3, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform4fv(GLuint program, GLint location, GLsizei count, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniform4fv(%d, %d, %p) =>(%f %f, %f, %f)\n", location, count, value, value[0], value[1], value[2],
                  value[3]);)
    PUSH_IF_COMPILING(glUniform4fv);
    CHECK_PROGRAM(void, program);
    GoUniformfv(glprogram, location, 4, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform1iv(GLuint program, GLint location, GLsizei count, const GLint* value) {
    PUSH_IF_COMPILING(glUniform1iv);
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 1, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform2iv(GLuint program, GLint location, GLsizei count, const GLint* value) {
    PUSH_IF_COMPILING(glUniform2iv);
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 2, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform3iv(GLuint program, GLint location, GLsizei count, const GLint* value) {
    PUSH_IF_COMPILING(glUniform3iv);
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 3, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform4iv(GLuint program, GLint location, GLsizei count, const GLint* value) {
    PUSH_IF_COMPILING(glUniform4iv);
    CHECK_PROGRAM(void, program);
    GoUniformiv(glprogram, location, 4, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform1uiv(GLuint program, GLint location, GLsizei count, const GLuint* value) {
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 1, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform2uiv(GLuint program, GLint location, GLsizei count, const GLuint* value) {
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 2, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform3uiv(GLuint program, GLint location, GLsizei count, const GLuint* value) {
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 3, count, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniform4uiv(GLuint program, GLint location, GLsizei count, const GLuint* value) {
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);
    GoUniformuiv(glprogram, location, 4, count, value);
}

void APIENTRY_GL4ES gl4es_glProgramUniformMatrix2fv(GLuint program, GLint location, GLsizei count, GLboolean transpose,
                                                    const GLfloat* value) {
    DBG(SHUT_LOGD("glUniformMatrix2fv(%d, %d, %d, %p)\n", location, count, transpose, value);)
    PUSH_IF_COMPILING(glUniformMatrix2fv);
    CHECK_PROGRAM(void, program);
    GoUniformMatrix2fv(glprogram, location, count, transpose, value);
}

void GoUniformMatrix2fv(program_t* glprogram, GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value) {
    DBG(SHUT_LOGD("GoUniformMatrix2fv(%p[%d], %d, %d, %d, %p)\n", glprogram, glprogram->id, location, count, transpose,
                  value);)
    if (location == -1) {
        noerrorShim();
        return;
    }
    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    khint_t k;
    uniform_t* m;
    k = kh_get(uniformlist, glprogram->uniform, location);
    if (k == kh_end(glprogram->uniform)) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    m = kh_value(glprogram->uniform, k);
    if (m->type != GL_FLOAT_MAT2 || count > m->size) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }
    // transpose if needed
    GLfloat* v = (GLfloat*)value;
    GLfloat stack_tmp[64];
    GLfloat* heap_tmp = NULL;
    if (transpose) {
        size_t total = (size_t)count * 4;
        if (total <= 64) {
            v = stack_tmp;
        } else {
            heap_tmp = (GLfloat*)malloc(total * sizeof(GLfloat));
            if (!heap_tmp) {
                errorShim(GL_OUT_OF_MEMORY);
                return;
            }
            v = heap_tmp;
        }
        for (int n = 0; n < count; n++)
            for (int i = 0; i < 2; i++)
                for (int j = 0; j < 2; j++)
                    v[n * 2 * 2 + i * 2 + j] = value[n * 2 * 2 + i + j * 2];
    }
    if (!g_emesis_internal_uniform_upload) {
        m->app_set = 1;
    }
    // ok, check the value in the cache BEFORE switching active GLES program
    int rsize = sizeof(GLfloat) * 2 * 2 * count;
    if (memcmp((char*)glprogram->cache.cache + m->cache_offs, v, rsize) == 0) {
        if (heap_tmp) free(heap_tmp);
        noerrorShim();
        return; // nothing to do, same value already there
    }
    // update uniform
    memcpy((char*)glprogram->cache.cache + m->cache_offs, v, rsize);
    m->last_mod_gen = ++glprogram->uniform_gen;
    if (glstate->gleshard->program != glprogram->id && !glprogram->last_comp) {
        queue_pending_uniform(glprogram, m, count);
        if (heap_tmp) free(heap_tmp);
        noerrorShim();
        return;
    }
    APPLY_PROGRAM(glprogram->id, glprogram);
    if (m->pending_upload && count >= (int)m->pending_elems) {
        m->pending_upload = 0;
        m->pending_elems = 0;
    }
    LOAD_GLES2(glUniformMatrix2fv);
    if (gles_glUniformMatrix2fv) {
        gles_glUniformMatrix2fv(m->id, count, GL_FALSE, v);
        errorGL();
    } else
        errorShim(GL_INVALID_OPERATION); // no GLSL hardware
    if (heap_tmp) free(heap_tmp);
}

void APIENTRY_GL4ES gl4es_glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniformMatrix3fv(%d, %d, %d, %p)\n", location, count, transpose, value);)
    PUSH_IF_COMPILING(glUniformMatrix3fv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformMatrix3fv(glprogram, location, count, transpose, value);
}

void APIENTRY_GL4ES gl4es_glProgramUniformMatrix3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose,
                                                    const GLfloat* value) {
    DBG(SHUT_LOGD("glUniformMatrix3fv(%d, %d, %d, %p)\n", location, count, transpose, value);)
    PUSH_IF_COMPILING(glUniformMatrix3fv);
    CHECK_PROGRAM(void, program);
    GoUniformMatrix3fv(glprogram, location, count, transpose, value);
}

void GoUniformMatrix3fv(program_t* glprogram, GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value) {
    DBG(SHUT_LOGD("GoUniformMatrix3fv(%p[%d], %d, %d, %d, %p)\n", glprogram, glprogram->id, location, count, transpose,
                  value);)
    if (location == -1) {
        noerrorShim();
        return;
    }
    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    khint_t k;
    uniform_t* m;
    k = kh_get(uniformlist, glprogram->uniform, location);
    if (k == kh_end(glprogram->uniform)) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    m = kh_value(glprogram->uniform, k);
    if (m->type != GL_FLOAT_MAT3 || count > m->size) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }
    // transpose if needed
    GLfloat* v = (GLfloat*)value;
    GLfloat stack_tmp[72];
    GLfloat* heap_tmp = NULL;
    if (transpose) {
        size_t total = (size_t)count * 9;
        if (total <= 72) {
            v = stack_tmp;
        } else {
            heap_tmp = (GLfloat*)malloc(total * sizeof(GLfloat));
            if (!heap_tmp) {
                errorShim(GL_OUT_OF_MEMORY);
                return;
            }
            v = heap_tmp;
        }
        for (int n = 0; n < count; n++)
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    v[n * 3 * 3 + i * 3 + j] = value[n * 3 * 3 + i + j * 3];
    }
    if (!g_emesis_internal_uniform_upload) {
        m->app_set = 1;
    }
    // ok, check the value in the cache BEFORE switching active GLES program
    int rsize = sizeof(GLfloat) * 3 * 3 * count;
    if (memcmp((char*)glprogram->cache.cache + m->cache_offs, v, rsize) == 0) {
        if (heap_tmp) free(heap_tmp);
        noerrorShim();
        return; // nothing to do, same value already there
    }
    // update uniform
    memcpy((char*)glprogram->cache.cache + m->cache_offs, v, rsize);
    m->last_mod_gen = ++glprogram->uniform_gen;
    if (glstate->gleshard->program != glprogram->id && !glprogram->last_comp) {
        queue_pending_uniform(glprogram, m, count);
        if (heap_tmp) free(heap_tmp);
        noerrorShim();
        return;
    }
    APPLY_PROGRAM(glprogram->id, glprogram);
    if (m->pending_upload && count >= (int)m->pending_elems) {
        m->pending_upload = 0;
        m->pending_elems = 0;
    }
    LOAD_GLES2(glUniformMatrix3fv);
    if (gles_glUniformMatrix3fv) {
        gles_glUniformMatrix3fv(m->id, count, GL_FALSE, v);
        errorGL();
    } else
        errorShim(GL_INVALID_OPERATION); // no GLSL hardware
    if (heap_tmp) free(heap_tmp);
}
void APIENTRY_GL4ES gl4es_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    DBG(SHUT_LOGD("glUniformMatrix4fv(%d, %d, %d, %p) p=>(%f, %f, %f, %f, %f...)\n", location, count, transpose, value,
                  value[0], value[1], value[2], value[3], value[4]);)
    PUSH_IF_COMPILING(glUniformMatrix4fv);
    GLuint program = glstate->glsl->program;
    CHECK_PROGRAM(void, program);
    GoUniformMatrix4fv(glprogram, location, count, transpose, value);
}
void APIENTRY_GL4ES gl4es_glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose,
                                                    const GLfloat* value) {
    DBG(SHUT_LOGD("glUniformMatrix4fv(%d, %d, %d, %p) p=>(%f, %f, %f, %f, %f...)\n", location, count, transpose, value,
                  value[0], value[1], value[2], value[3], value[4]);)
    PUSH_IF_COMPILING(glUniformMatrix4fv);
    CHECK_PROGRAM(void, program);
    GoUniformMatrix4fv(glprogram, location, count, transpose, value);
}

void GoUniformMatrix4fv(program_t* glprogram, GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value) {
    DBG(SHUT_LOGD("GoUniformMatrix4fv(%p[%d], %d, %d, %d, %p) p=>(%f, %f, %f, %f, %f...)\n", glprogram, glprogram->id,
                  location, count, transpose, value, value[0], value[1], value[2], value[3], value[4]);)
    if (location == -1) {
        noerrorShim();
        return;
    }
    if (count < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    khint_t k;
    uniform_t* m;
    k = kh_get(uniformlist, glprogram->uniform, location);
    if (k == kh_end(glprogram->uniform)) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    m = kh_value(glprogram->uniform, k);
    if (m->type != GL_FLOAT_MAT4 || count > m->size) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }
    // transpose if needed
    GLfloat* v = (GLfloat*)value;
    GLfloat stack_tmp[64];
    GLfloat* heap_tmp = NULL;
    if (transpose) {
        size_t total = (size_t)count * 16;
        if (total <= 64) {
            v = stack_tmp;
        } else {
            heap_tmp = (GLfloat*)malloc(total * sizeof(GLfloat));
            if (!heap_tmp) {
                errorShim(GL_OUT_OF_MEMORY);
                return;
            }
            v = heap_tmp;
        }
        for (int n = 0; n < count; n++)
            matrix_transpose(value + n * 4 * 4, v + n * 4 * 4);
    }
    if (!g_emesis_internal_uniform_upload) {
        m->app_set = 1;
        if (m->sm_source_id) {
            emesis_sm_observe_mat4(m->sm_source_id, count, v);
        }
    }
    // ok, check the value in the cache BEFORE switching active GLES program
    int rsize = sizeof(GLfloat) * 4 * 4 * count;
    if (memcmp((char*)glprogram->cache.cache + m->cache_offs, v, rsize) == 0) {
        if (heap_tmp) free(heap_tmp);
        noerrorShim();
        return; // nothing to do, same value already there
    }
    // update uniform
    memcpy((char*)glprogram->cache.cache + m->cache_offs, v, rsize);
    m->last_mod_gen = ++glprogram->uniform_gen;
    if (glstate->gleshard->program != glprogram->id && !glprogram->last_comp) {
        queue_pending_uniform(glprogram, m, count);
        if (heap_tmp) free(heap_tmp);
        noerrorShim();
        return;
    }
    APPLY_PROGRAM(glprogram->id, glprogram);
    if (m->pending_upload && count >= (int)m->pending_elems) {
        m->pending_upload = 0;
        m->pending_elems = 0;
    }
    LOAD_GLES2(glUniformMatrix4fv);
    if (gles_glUniformMatrix4fv) {
        gles_glUniformMatrix4fv(m->id, count, GL_FALSE, v);
        errorGL();
    } else {
        errorShim(GL_INVALID_OPERATION); // no GLSL hardware
    }
    if (heap_tmp) free(heap_tmp);
}

int GetUniformi(program_t* glprogram, GLint location) {
    if (location == -1) {
        noerrorShim();
        return 0;
    }

    khint_t k;
    uniform_t* m;
    k = kh_get(uniformlist, glprogram->uniform, location);
    if (k == kh_end(glprogram->uniform)) {
        return 0;
    }
    m = kh_value(glprogram->uniform, k);

    // ok, grab the value in the cache
    GLint ret;
    memcpy(&ret, (char*)glprogram->cache.cache + m->cache_offs, sizeof(GLint));
    return ret;
}

const char* GetUniformName(program_t* glprogram, GLint location) {
    if (location == -1) {
        noerrorShim();
        return 0;
    }

    khint_t k;
    uniform_t* m;
    k = kh_get(uniformlist, glprogram->uniform, location);
    if (k == kh_end(glprogram->uniform)) {
        return 0;
    }
    m = kh_value(glprogram->uniform, k);

    // ok, grab the value in the cache
    return m->name;
}

AliasExport(void, glGetUniformfv, , (GLuint program, GLint location, GLfloat* params));
AliasExport(void, glGetUniformiv, , (GLuint program, GLint location, GLint* params));
AliasExport(void, glGetUniformuiv, , (GLuint program, GLint location, GLuint* params));
AliasExport(void, glGetUniformuiv, EXT, (GLuint program, GLint location, GLuint* params));
AliasExport(void, glUniform1f, , (GLint location, GLfloat v0));
AliasExport(void, glUniform2f, , (GLint location, GLfloat v0, GLfloat v1));
AliasExport(void, glUniform3f, , (GLint location, GLfloat v0, GLfloat v1, GLfloat v2));
AliasExport(void, glUniform4f, , (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3));
AliasExport(void, glUniform1i, , (GLint location, GLint v0));
AliasExport(void, glUniform2i, , (GLint location, GLint v0, GLint v1));
AliasExport(void, glUniform3i, , (GLint location, GLint v0, GLint v1, GLint v2));
AliasExport(void, glUniform4i, , (GLint location, GLint v0, GLint v1, GLint v2, GLint v3));
AliasExport(void, glUniform1ui, , (GLint location, GLuint v0));
AliasExport(void, glUniform2ui, , (GLint location, GLuint v0, GLuint v1));
AliasExport(void, glUniform3ui, , (GLint location, GLuint v0, GLuint v1, GLuint v2));
AliasExport(void, glUniform4ui, , (GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3));
AliasExport(void, glUniform1ui, EXT, (GLint location, GLuint v0));
AliasExport(void, glUniform2ui, EXT, (GLint location, GLuint v0, GLuint v1));
AliasExport(void, glUniform3ui, EXT, (GLint location, GLuint v0, GLuint v1, GLuint v2));
AliasExport(void, glUniform4ui, EXT, (GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3));
AliasExport(void, glUniform1fv, , (GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glUniform2fv, , (GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glUniform3fv, , (GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glUniform4fv, , (GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glUniform1iv, , (GLint location, GLsizei count, const GLint* value));
AliasExport(void, glUniform2iv, , (GLint location, GLsizei count, const GLint* value));
AliasExport(void, glUniform3iv, , (GLint location, GLsizei count, const GLint* value));
AliasExport(void, glUniform4iv, , (GLint location, GLsizei count, const GLint* value));
AliasExport(void, glUniform1uiv, , (GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glUniform2uiv, , (GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glUniform3uiv, , (GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glUniform4uiv, , (GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glUniform1uiv, EXT, (GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glUniform2uiv, EXT, (GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glUniform3uiv, EXT, (GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glUniform4uiv, EXT, (GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glUniformMatrix2fv, , (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));
AliasExport(void, glUniformMatrix3fv, , (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));
AliasExport(void, glUniformMatrix4fv, , (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));

// ============ GL_ARB_shader_objects ================

AliasExport(GLvoid, glUniform1f, ARB, (GLint location, GLfloat v0));
AliasExport(GLvoid, glUniform2f, ARB, (GLint location, GLfloat v0, GLfloat v1));
AliasExport(GLvoid, glUniform3f, ARB, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2));
AliasExport(GLvoid, glUniform4f, ARB, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3));

AliasExport(GLvoid, glUniform1i, ARB, (GLint location, GLint v0));
AliasExport(GLvoid, glUniform2i, ARB, (GLint location, GLint v0, GLint v1));
AliasExport(GLvoid, glUniform3i, ARB, (GLint location, GLint v0, GLint v1, GLint v2));
AliasExport(GLvoid, glUniform4i, ARB, (GLint location, GLint v0, GLint v1, GLint v2, GLint v3));

AliasExport(GLvoid, glUniform1fv, ARB, (GLint location, GLsizei count, const GLfloat* value));
AliasExport(GLvoid, glUniform2fv, ARB, (GLint location, GLsizei count, const GLfloat* value));
AliasExport(GLvoid, glUniform3fv, ARB, (GLint location, GLsizei count, const GLfloat* value));
AliasExport(GLvoid, glUniform4fv, ARB, (GLint location, GLsizei count, const GLfloat* value));

AliasExport(GLvoid, glUniform1iv, ARB, (GLint location, GLsizei count, const GLint* value));
AliasExport(GLvoid, glUniform2iv, ARB, (GLint location, GLsizei count, const GLint* value));
AliasExport(GLvoid, glUniform3iv, ARB, (GLint location, GLsizei count, const GLint* value));
AliasExport(GLvoid, glUniform4iv, ARB, (GLint location, GLsizei count, const GLint* value));

AliasExport(GLvoid, glUniformMatrix2fv, ARB,
            (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));
AliasExport(GLvoid, glUniformMatrix3fv, ARB,
            (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));
AliasExport(GLvoid, glUniformMatrix4fv, ARB,
            (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));

AliasExport(GLvoid, glGetUniformfv, ARB, (GLhandleARB programObj, GLint location, GLfloat* params));
AliasExport(GLvoid, glGetUniformiv, ARB, (GLhandleARB programObj, GLint location, GLint* params));

// ===============
AliasExport(void, glProgramUniform1f, , (GLuint program, GLint location, GLfloat v0));
AliasExport(void, glProgramUniform2f, , (GLuint program, GLint location, GLfloat v0, GLfloat v1));
AliasExport(void, glProgramUniform3f, , (GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2));
AliasExport(void, glProgramUniform4f, ,
            (GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3));
AliasExport(void, glProgramUniform1i, , (GLuint program, GLint location, GLint v0));
AliasExport(void, glProgramUniform2i, , (GLuint program, GLint location, GLint v0, GLint v1));
AliasExport(void, glProgramUniform3i, , (GLuint program, GLint location, GLint v0, GLint v1, GLint v2));
AliasExport(void, glProgramUniform4i, , (GLuint program, GLint location, GLint v0, GLint v1, GLint v2, GLint v3));
AliasExport(void, glProgramUniform1fv, , (GLuint program, GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glProgramUniform2fv, , (GLuint program, GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glProgramUniform3fv, , (GLuint program, GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glProgramUniform4fv, , (GLuint program, GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glProgramUniform1iv, , (GLuint program, GLint location, GLsizei count, const GLint* value));
AliasExport(void, glProgramUniform2iv, , (GLuint program, GLint location, GLsizei count, const GLint* value));
AliasExport(void, glProgramUniform3iv, , (GLuint program, GLint location, GLsizei count, const GLint* value));
AliasExport(void, glProgramUniform4iv, , (GLuint program, GLint location, GLsizei count, const GLint* value));
AliasExport(void, glProgramUniform1ui, , (GLuint program, GLint location, GLuint v0));
AliasExport(void, glProgramUniform2ui, , (GLuint program, GLint location, GLuint v0, GLuint v1));
AliasExport(void, glProgramUniform3ui, , (GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2));
AliasExport(void, glProgramUniform4ui, , (GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3));
AliasExport(void, glProgramUniform1uiv, , (GLuint program, GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glProgramUniform2uiv, , (GLuint program, GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glProgramUniform3uiv, , (GLuint program, GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glProgramUniform4uiv, , (GLuint program, GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glProgramUniform1ui, EXT, (GLuint program, GLint location, GLuint v0));
AliasExport(void, glProgramUniform2ui, EXT, (GLuint program, GLint location, GLuint v0, GLuint v1));
AliasExport(void, glProgramUniform3ui, EXT, (GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2));
AliasExport(void, glProgramUniform4ui, EXT, (GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3));
AliasExport(void, glProgramUniform1uiv, EXT, (GLuint program, GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glProgramUniform2uiv, EXT, (GLuint program, GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glProgramUniform3uiv, EXT, (GLuint program, GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glProgramUniform4uiv, EXT, (GLuint program, GLint location, GLsizei count, const GLuint* value));
AliasExport(void, glProgramUniformMatrix2fv, ,
            (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));
AliasExport(void, glProgramUniformMatrix3fv, ,
            (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));
AliasExport(void, glProgramUniformMatrix4fv, ,
            (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));
// ===============	EXT_direct_state_access (part of it)
AliasExport(void, glProgramUniform1f, EXT, (GLuint program, GLint location, GLfloat v0));
AliasExport(void, glProgramUniform2f, EXT, (GLuint program, GLint location, GLfloat v0, GLfloat v1));
AliasExport(void, glProgramUniform3f, EXT, (GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2));
AliasExport(void, glProgramUniform4f, EXT,
            (GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3));
AliasExport(void, glProgramUniform1i, EXT, (GLuint program, GLint location, GLint v0));
AliasExport(void, glProgramUniform2i, EXT, (GLuint program, GLint location, GLint v0, GLint v1));
AliasExport(void, glProgramUniform3i, EXT, (GLuint program, GLint location, GLint v0, GLint v1, GLint v2));
AliasExport(void, glProgramUniform4i, EXT, (GLuint program, GLint location, GLint v0, GLint v1, GLint v2, GLint v3));
AliasExport(void, glProgramUniform1fv, EXT, (GLuint program, GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glProgramUniform2fv, EXT, (GLuint program, GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glProgramUniform3fv, EXT, (GLuint program, GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glProgramUniform4fv, EXT, (GLuint program, GLint location, GLsizei count, const GLfloat* value));
AliasExport(void, glProgramUniform1iv, EXT, (GLuint program, GLint location, GLsizei count, const GLint* value));
AliasExport(void, glProgramUniform2iv, EXT, (GLuint program, GLint location, GLsizei count, const GLint* value));
AliasExport(void, glProgramUniform3iv, EXT, (GLuint program, GLint location, GLsizei count, const GLint* value));
AliasExport(void, glProgramUniform4iv, EXT, (GLuint program, GLint location, GLsizei count, const GLint* value));
AliasExport(void, glProgramUniformMatrix2fv, EXT,
            (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));
AliasExport(void, glProgramUniformMatrix3fv, EXT,
            (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));
AliasExport(void, glProgramUniformMatrix4fv, EXT,
            (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));

static void GoUniformMatrixNonSquare(program_t* glprogram, GLint location, GLenum mat_type, int cols, int rows,
                                     GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (!glprogram) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (location == -1) {
        noerrorShim();
        return;
    }
    if (count < 0 || !value) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    khint_t k = kh_get(uniformlist, glprogram->uniform, location);
    if (k == kh_end(glprogram->uniform)) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    uniform_t* m = kh_value(glprogram->uniform, k);
    if (m->type != mat_type || count > m->size) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (count == 0) {
        noerrorShim();
        return;
    }
    int elems = cols * rows;
    const GLfloat* v = value;
    GLfloat stack_tmp[96];
    GLfloat* heap_tmp = NULL;
    if (transpose) {
        size_t total = (size_t)count * (size_t)elems;
        GLfloat* dst = (total <= 96) ? stack_tmp : (heap_tmp = (GLfloat*)malloc(total * sizeof(GLfloat)));
        if (!dst) {
            errorShim(GL_OUT_OF_MEMORY);
            return;
        }
        for (int n = 0; n < count; ++n) {
            int base = n * elems;
            for (int c = 0; c < cols; ++c) {
                for (int r = 0; r < rows; ++r) {
                    dst[base + c * rows + r] = value[base + r * cols + c];
                }
            }
        }
        v = dst;
    }
    if (!g_emesis_internal_uniform_upload) {
        m->app_set = 1;
    }
    size_t rsize = sizeof(GLfloat) * (size_t)elems * (size_t)count;
    if (memcmp((char*)glprogram->cache.cache + m->cache_offs, v, rsize) == 0) {
        if (heap_tmp) free(heap_tmp);
        noerrorShim();
        return;
    }
    memcpy((char*)glprogram->cache.cache + m->cache_offs, v, rsize);
    m->last_mod_gen = ++glprogram->uniform_gen;
    if (glstate->gleshard->program != glprogram->id && !glprogram->last_comp) {
        queue_pending_uniform(glprogram, m, count);
        if (heap_tmp) free(heap_tmp);
        noerrorShim();
        return;
    }
    APPLY_PROGRAM(glprogram->id, glprogram);
    if (m->pending_upload && count >= (int)m->pending_elems) {
        m->pending_upload = 0;
        m->pending_elems = 0;
    }
    switch (mat_type) {
    case 0x8B65: {
        LOAD_GLES3(glUniformMatrix2x3fv);
        if (gles_glUniformMatrix2x3fv) gles_glUniformMatrix2x3fv(m->id, count, GL_FALSE, v);
        break;
    }
    case 0x8B66: {
        LOAD_GLES3(glUniformMatrix2x4fv);
        if (gles_glUniformMatrix2x4fv) gles_glUniformMatrix2x4fv(m->id, count, GL_FALSE, v);
        break;
    }
    case 0x8B67: {
        LOAD_GLES3(glUniformMatrix3x2fv);
        if (gles_glUniformMatrix3x2fv) gles_glUniformMatrix3x2fv(m->id, count, GL_FALSE, v);
        break;
    }
    case 0x8B68: {
        LOAD_GLES3(glUniformMatrix3x4fv);
        if (gles_glUniformMatrix3x4fv) gles_glUniformMatrix3x4fv(m->id, count, GL_FALSE, v);
        break;
    }
    case 0x8B69: {
        LOAD_GLES3(glUniformMatrix4x2fv);
        if (gles_glUniformMatrix4x2fv) gles_glUniformMatrix4x2fv(m->id, count, GL_FALSE, v);
        break;
    }
    case 0x8B6A: {
        LOAD_GLES3(glUniformMatrix4x3fv);
        if (gles_glUniformMatrix4x3fv) gles_glUniformMatrix4x3fv(m->id, count, GL_FALSE, v);
        break;
    }
    }
    if (heap_tmp) free(heap_tmp);
    errorGL();
}

#define DEF_NONSQUARE_MAT(NAME, ENUM_VAL, COLS, ROWS)                                                                  \
    void APIENTRY_GL4ES gl4es_glUniformMatrix##NAME##fv(GLint location, GLsizei count, GLboolean transpose,            \
                                                        const GLfloat* value) {                                        \
        FLUSH_BEGINEND;                                                                                                \
        GLuint program = glstate->glsl->program;                                                                       \
        CHECK_PROGRAM(void, program);                                                                                  \
        GoUniformMatrixNonSquare(glprogram, location, ENUM_VAL, COLS, ROWS, count, transpose, value);                  \
    }                                                                                                                  \
    void APIENTRY_GL4ES gl4es_glProgramUniformMatrix##NAME##fv(GLuint program, GLint location, GLsizei count,          \
                                                               GLboolean transpose, const GLfloat* value) {            \
        FLUSH_BEGINEND;                                                                                                \
        CHECK_PROGRAM(void, program);                                                                                  \
        GoUniformMatrixNonSquare(glprogram, location, ENUM_VAL, COLS, ROWS, count, transpose, value);                  \
    }                                                                                                                  \
    AliasExport(void, glUniformMatrix##NAME##fv, ,                                                                     \
                (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));                           \
    AliasExport(void, glProgramUniformMatrix##NAME##fv, ,                                                              \
                (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));           \
    AliasExport(void, glProgramUniformMatrix##NAME##fv, EXT,                                                           \
                (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value));

DEF_NONSQUARE_MAT(2x3, 0x8B65, 2, 3)
DEF_NONSQUARE_MAT(2x4, 0x8B66, 2, 4)
DEF_NONSQUARE_MAT(3x2, 0x8B67, 3, 2)
DEF_NONSQUARE_MAT(3x4, 0x8B68, 3, 4)
DEF_NONSQUARE_MAT(4x2, 0x8B69, 4, 2)
DEF_NONSQUARE_MAT(4x3, 0x8B6A, 4, 3)
#undef DEF_NONSQUARE_MAT

void APIENTRY_GL4ES gl4es_glGetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar* const* uniformNames,
                                              GLuint* uniformIndices) {
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);
    if (uniformCount < 0 || (uniformCount > 0 && (!uniformNames || !uniformIndices))) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (uniformCount == 0) {
        noerrorShim();
        return;
    }
    if (hardext.esversion >= 3) {
        LOAD_GLES3(glGetUniformIndices);
        if (gles_glGetUniformIndices) {
            errorGL();
            gles_glGetUniformIndices(glprogram->id, uniformCount, uniformNames, uniformIndices);
            return;
        }
    }
    for (GLsizei i = 0; i < uniformCount; ++i) {
        GLuint found = GL_INVALID_INDEX;
        const char* target = uniformNames[i];
        if (target) {
            uniform_t* m;
            kh_foreach_value(glprogram->uniform, m, {
                if (!m->builtin && m->name && strcmp(m->name, target) == 0) {
                    found = (GLuint)m->internal_id;
                    break;
                }
            });
        }
        uniformIndices[i] = found;
    }
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glGetActiveUniformsiv(GLuint program, GLsizei uniformCount, const GLuint* uniformIndices,
                                                GLenum pname, GLint* params) {
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program);
    if (uniformCount < 0 || (uniformCount > 0 && (!uniformIndices || !params))) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (uniformCount == 0) {
        noerrorShim();
        return;
    }
    if (hardext.esversion >= 3) {
        LOAD_GLES3(glGetActiveUniformsiv);
        if (gles_glGetActiveUniformsiv) {
            errorGL();
            gles_glGetActiveUniformsiv(glprogram->id, uniformCount, uniformIndices, pname, params);
            return;
        }
    }
    for (GLsizei i = 0; i < uniformCount; ++i) {
        uniform_t* found = NULL;
        uniform_t* m;
        kh_foreach_value(glprogram->uniform, m, {
            if ((GLuint)m->internal_id == uniformIndices[i]) {
                found = m;
                break;
            }
        });
        if (!found) {
            params[i] = -1;
            continue;
        }
        switch (pname) {
        case GL_UNIFORM_TYPE:
            params[i] = (GLint)found->type;
            break;
        case GL_UNIFORM_SIZE:
            params[i] = (GLint)found->size;
            break;
        case GL_UNIFORM_NAME_LENGTH:
            params[i] = found->name ? (GLint)(strlen(found->name) + 1) : 0;
            break;
        case GL_UNIFORM_BLOCK_INDEX:
            params[i] = -1;
            break;
        case GL_UNIFORM_OFFSET:
        case GL_UNIFORM_ARRAY_STRIDE:
        case GL_UNIFORM_MATRIX_STRIDE:
        case GL_UNIFORM_IS_ROW_MAJOR:
            params[i] = 0;
            break;
        default:
            errorShim(GL_INVALID_ENUM);
            return;
        }
    }
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glGetActiveUniformName(GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei* length,
                                                 GLchar* uniformName) {
    GLint dummy_size = 0;
    GLenum dummy_type = 0;
    gl4es_glGetActiveUniform(program, uniformIndex, bufSize, length, &dummy_size, &dummy_type, uniformName);
}

AliasExport(void, glGetUniformIndices, ,
            (GLuint program, GLsizei uniformCount, const GLchar* const* uniformNames, GLuint* uniformIndices));
AliasExport(void, glGetActiveUniformsiv, ,
            (GLuint program, GLsizei uniformCount, const GLuint* uniformIndices, GLenum pname, GLint* params));
AliasExport(void, glGetActiveUniformName, ,
            (GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei* length, GLchar* uniformName));
