#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "program.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <regex.h>
#include "../glx/hardext.h"
#include "debug.h"
#include "fpe.h"
#include "gl4es.h"
#include "glstate.h"
#include "loader.h"
#include "shaderconv.h"
#include "shader_cache.h"
#include "shadermod_uniforms.h"
#include "fpe_shader.h"
#include "string_utils.h"

// #define DEBUG
#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

// KH Map implementations
KHASH_MAP_IMPL_INT(attribloclist, attribloc_t*);
KHASH_MAP_IMPL_INT(uniformlist, uniform_t*);
KHASH_MAP_IMPL_STR(uniformnamelist, GLint);
KHASH_MAP_IMPL_INT(programlist, program_t*);

void VISIBLE glBindFragDataLocation(GLuint program, GLuint colorNumber, const GLchar* name) {
    DBG(SHUT_LOGD("glBindFragDataLocation(%d, %d, \"%s\")\n", program, colorNumber, name))
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program)
    if (!name || !name[0]) return;
    if (glprogram->last_frag && glprogram->last_frag->converted) {
        if (!glprogram->last_frag->before_patch) {
            glprogram->last_frag->before_patch = strdup(glprogram->last_frag->converted);
        }
        size_t len = strlen(name);
        size_t tlen = len + 48;
        char* targetPattern = (char*)malloc(tlen);
        if (!targetPattern) return;
        snprintf(targetPattern, tlen, "out[ ]+[A-Za-z0-9_ ]+[ ]+%s", name);

        regex_t regex;
        regmatch_t pmatch[1];
        if (regcomp(&regex, targetPattern, REG_EXTENDED) != 0) {
            free(targetPattern);
            return;
        }
        free(targetPattern);

        size_t searchOffset = 0;
        while (regexec(&regex, glprogram->last_frag->converted + searchOffset, 1, pmatch, 0) == 0) {
            size_t matchStart = searchOffset + (size_t)pmatch[0].rm_so;
            size_t matchLen = (size_t)(pmatch[0].rm_eo - pmatch[0].rm_so);

            /* Avoid double-inserting `layout (location = ...)` if already preceded by `)` */
            if (matchStart > 0) {
                size_t back = matchStart;
                while (back > 0 && (glprogram->last_frag->converted[back - 1] == ' ' ||
                                    glprogram->last_frag->converted[back - 1] == '\t')) {
                    back--;
                }
                if (back > 0 && glprogram->last_frag->converted[back - 1] == ')') {
                    searchOffset = matchStart + matchLen;
                    continue;
                }
            }

            char prefixLayout[48];
            int layoutLen = snprintf(prefixLayout, sizeof(prefixLayout), "layout (location = %u) ", colorNumber);
            if (layoutLen <= 0) break;

            size_t curLen = strlen(glprogram->last_frag->converted);
            char* newConverted = (char*)malloc(curLen + (size_t)layoutLen + 1);
            if (!newConverted) break;

            memcpy(newConverted, glprogram->last_frag->converted, matchStart);
            memcpy(newConverted + matchStart, prefixLayout, (size_t)layoutLen);
            memcpy(newConverted + matchStart + (size_t)layoutLen,
                   glprogram->last_frag->converted + matchStart,
                   curLen - matchStart + 1);

            free(glprogram->last_frag->converted);
            glprogram->last_frag->converted = newConverted;
            searchOffset = matchStart + (size_t)layoutLen + matchLen;
        }

        regfree(&regex);
        glprogram->frag_data_changed = 1;
    }
    /*if (!program) {
        noerrorShim();
        return;
    }
    program_t* glprogram = NULL;
    khint_t k_program;
    {
        int ret;
        khash_t(programlist)* programs = glstate->glsl->programs;
        k_program = kh_get(programlist, programs, program);
        if (k_program != kh_end(programs)) {
            glprogram = kh_value(programs, k_program);
        }
    }
    if (!glprogram) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    shader_t* frag_shader = glprogram->last_frag;
    if (!frag_shader) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    GLint location = -1;
    for (int i = 0; i < glprogram->num_uniform; i++) {
        uniform_t* uniform = &glprogram->uniform[i];
        if (strcmp(uniform->name, name) == 0) {
            location = i;
            break;
        }
    }
    if (location == -1) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    khash_t(attribloclist)* attribloc = glprogram->attribloc;
    khint_t k = kh_get(attribloclist, attribloc, colorNumber);
    if (k == kh_end(attribloc)) {
        attribloc_t* new_attribloc = (attribloc_t*)malloc(sizeof(attribloc_t));
        new_attribloc->index = colorNumber;
        new_attribloc->real_index = location;
        new_attribloc->name = strdup(name);
        kh_put(attribloclist, attribloc, colorNumber, &k);
        kh_value(attribloc, k) = new_attribloc;
    }
    else {
        attribloc_t* existing_attribloc = kh_value(attribloc, k);
        existing_attribloc->real_index = location;
        existing_attribloc->name = strdup(name);
    }
    glprogram->linked = 1;
    noerrorShim();*/
}


void APIENTRY_GL4ES gl4es_glAttachShader(GLuint program, GLuint shader) {
    DBG(SHUT_LOGD("glAttachShader(%d, %d)\n", program, shader))
    FLUSH_BEGINEND;
    // sanity tests
    CHECK_PROGRAM(void, program)
    CHECK_SHADER(void, shader)

    // add shader reference to program
    if (glprogram->attach_cap == glprogram->attach_size) {
        glprogram->attach_cap += 4;
        glprogram->attach = (GLuint*)realloc(glprogram->attach, sizeof(GLuint) * glprogram->attach_cap);
    }
    glprogram->attach[glprogram->attach_size++] = glshader->id;
    glshader->attached++;
    // save last vertex or fragment attached
    if (glshader->type == GL_VERTEX_SHADER && !glprogram->last_vert)
        glprogram->last_vert = glshader;
    else if (glshader->type == GL_FRAGMENT_SHADER && !glprogram->last_frag)
        glprogram->last_frag = glshader;
    else if (glshader->type == GL_COMPUTE_SHADER)
        glprogram->last_comp = glshader;
    // merge uniforms_declarations
    uniform_decls_merge(&glprogram->declarations, &glshader->uniforms_declarations);
    /* Defer gles_glAttachShader until glLinkProgram misses the .ep1 program binary cache,
       avoiding attaching uncompiled GLES shader objects before glProgramBinary */
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    DBG(SHUT_LOGD("glBindAttribLocation(%d, %d, \"%s\")\n", program, index, name))
    FLUSH_BEGINEND;
    // sanity tests
    CHECK_PROGRAM(void, program)
    // look / create attribloc at index
    attribloc_t* attribloc = NULL;
    {
        khint_t k;
        int ret;
        khash_t(attribloclist)* attribloclist = glprogram->attribloc;
        k = kh_get(attribloclist, attribloclist, index);
        if (k != kh_end(attribloclist)) {
            attribloc = kh_value(attribloclist, k);
        } else {
            k = kh_put(attribloclist, attribloclist, index, &ret);
            attribloc = kh_value(attribloclist, k) = malloc(sizeof(attribloc_t));
            memset(attribloc, 0, sizeof(attribloc_t));
            attribloc->real_index = -1;
            attribloc->index = index;
        }
    }
    // update name
    if (attribloc->name) free(attribloc->name);
    attribloc->name = strdup(name);
    attribloc->glname = attribloc->name;
    // send to hardware
    LOAD_GLES2(glBindAttribLocation);
    if (gles_glBindAttribLocation) {
        gles_glBindAttribLocation(glprogram->id, index, attribloc->name);
        if (strcmp(attribloc->name, "texture") == 0) {
            gles_glBindAttribLocation(glprogram->id, index, "vgpu_texture");
        } else if (strcmp(attribloc->name, "sample") == 0) {
            gles_glBindAttribLocation(glprogram->id, index, "vgpu_Sample");
        }
        errorGL();
    } else
        noerrorShim();
}

void actually_deleteshader(GLuint shader);
void actually_detachshader(GLuint shader);

GLuint APIENTRY_GL4ES gl4es_glCreateProgram(void) {
    DBG(SHUT_LOGD("glCreateProgram()\n"))
    FLUSH_BEGINEND;
    static GLuint lastprogram = 0;
    GLuint program;
    // create the program
    LOAD_GLES2(glCreateProgram);
    if (gles_glCreateProgram) {
        program = gles_glCreateProgram();
        if (!program) {
            errorGL();
            return 0;
        }
    } else {
        program = ++lastprogram;
        noerrorShim();
    }
    // store the new empty shader in the list for later use
    khint_t k;
    int ret;
    khash_t(programlist)* programs = glstate->glsl->programs;
    k = kh_get(programlist, programs, program);
    program_t* glprogram = NULL;
    if (k == kh_end(programs)) {
        k = kh_put(programlist, programs, program, &ret);
        glprogram = kh_value(programs, k) = (program_t*)calloc(1, sizeof(program_t));
    } else {
        glprogram = kh_value(programs, k);
        for (int i = 0; i < glprogram->attach_size; i++) {
            actually_detachshader(glprogram->attach[i]);
        }
        if (glprogram->attribloc) {
            attribloc_t* m;
            kh_foreach_value(glprogram->attribloc, m, free(m->name); free(m);)
                kh_destroy(attribloclist, glprogram->attribloc);
            glprogram->attribloc = NULL;
        }
        if (glprogram->uniform_by_name) {
            kh_destroy(uniformnamelist, glprogram->uniform_by_name);
            glprogram->uniform_by_name = NULL;
        }
        if (glprogram->uniform) {
            uniform_t* m;
            kh_foreach_value(glprogram->uniform, m, free(m->name); free(m);)
                kh_destroy(uniformlist, glprogram->uniform);
            glprogram->uniform = NULL;
        }
        if (glprogram->cache.cache) free(glprogram->cache.cache);
        if (glprogram->vert_essl_copy) free(glprogram->vert_essl_copy);
        if (glprogram->frag_essl_copy) free(glprogram->frag_essl_copy);
        if (glprogram->parent_sync_list) free(glprogram->parent_sync_list);
        if (glprogram->pending_list) free(glprogram->pending_list);
        if (glprogram->custom_uniforms) free(glprogram->custom_uniforms);
        if (glprogram->attach) free(glprogram->attach);
        if (glprogram->default_need) free(glprogram->default_need);
        if (glprogram->fpe_cache) fpe_disposeCache((fpe_cache_t*)glprogram->fpe_cache, 1);
        if (glstate->glsl && (glstate->glsl->glprogram == glprogram || glstate->glsl->program == program)) {
            glstate->glsl->glprogram = NULL;
            glstate->glsl->program = 0;
        }
        if (glstate->gleshard && (glstate->gleshard->glprogram == glprogram || glstate->gleshard->program == program)) {
            glstate->gleshard->glprogram = NULL;
            glstate->gleshard->program = 0;
        }
        if (glstate->fpe && glstate->fpe->glprogram == glprogram) {
            glstate->fpe = NULL;
        }
        uniform_decls_clear(&glprogram->declarations);
        memset(glprogram, 0, sizeof(program_t));
    }
    glprogram->id = program;
    // initialize attribloc hashmap
    glprogram->attribloc = kh_init(attribloclist);
    // initialize uniform hashmaps
    glprogram->uniform = kh_init(uniformlist);
    glprogram->uniform_by_name = kh_init(uniformnamelist);
    // all done
    return program;
}

void deleteProgram(program_t* glprogram, khint_t k_program) {
    free(glprogram->attach);
    if (glprogram->default_need) {
        free(glprogram->default_need);
        glprogram->default_need = NULL;
    }
    uniform_decls_clear(&glprogram->declarations);
    // clean attribloc
    if (glprogram->attribloc) {
        attribloc_t* m;
        kh_foreach_value(glprogram->attribloc, m, free(m->name); free(m);)
            kh_destroy(attribloclist, glprogram->attribloc);
        glprogram->attribloc = NULL;
    }
    // clean uniform name index (keys are owned by uniform_t->name)
    if (glprogram->uniform_by_name) {
        kh_destroy(uniformnamelist, glprogram->uniform_by_name);
        glprogram->uniform_by_name = NULL;
    }
    // clean uniform list
    if (glprogram->uniform) {
        uniform_t* m;
        kh_foreach_value(glprogram->uniform, m, free(m->name); free(m);) kh_destroy(uniformlist, glprogram->uniform);
        glprogram->uniform = NULL;
    }
    // clean cache
    if (glprogram->cache.cache) free(glprogram->cache.cache);
    if (glprogram->vert_essl_copy) {
        free(glprogram->vert_essl_copy);
        glprogram->vert_essl_copy = NULL;
    }
    if (glprogram->frag_essl_copy) {
        free(glprogram->frag_essl_copy);
        glprogram->frag_essl_copy = NULL;
    }
    if (glprogram->parent_sync_list) {
        free(glprogram->parent_sync_list);
        glprogram->parent_sync_list = NULL;
    }
    if (glprogram->pending_list) {
        free(glprogram->pending_list);
        glprogram->pending_list = NULL;
    }
    if (glprogram->custom_uniforms) {
        free(glprogram->custom_uniforms);
        glprogram->custom_uniforms = NULL;
    }
    // clean fpe cache if it exist
    if (glprogram->fpe_cache) fpe_disposeCache((fpe_cache_t*)glprogram->fpe_cache, 1);
    // clear any active state pointers referencing this program before freeing
    if (glstate->glsl) {
        if (glstate->glsl->glprogram == glprogram || glstate->glsl->program == glprogram->id) {
            glstate->glsl->glprogram = NULL;
            glstate->glsl->program = 0;
        }
    }
    if (glstate->gleshard) {
        if (glstate->gleshard->glprogram == glprogram || glstate->gleshard->program == glprogram->id) {
            glstate->gleshard->glprogram = NULL;
            glstate->gleshard->program = 0;
        }
    }
    if (glstate->fpe && glstate->fpe->glprogram == glprogram) {
        glstate->fpe = NULL;
    }
    // delete program
    kh_del(programlist, glstate->glsl->programs, k_program);
    free(glprogram);
}

void APIENTRY_GL4ES gl4es_glDeleteProgram(GLuint program) {
    DBG(SHUT_LOGD("glDeleteProgram(%d)\n", program))
    if (!glstate) return; // in case a program delete shaders after deleteing all context
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program)
    // send to hardware
    LOAD_GLES2(glDeleteProgram);
    if (gles_glDeleteProgram) {
        gles_glDeleteProgram(glprogram->id);
        errorGL();
    } else
        noerrorShim();
    // clean attached shaders
    glprogram->last_vert = NULL;
    glprogram->last_frag = NULL;
    glprogram->last_comp = NULL;
    for (int i = 0; i < glprogram->attach_size; i++) {
        actually_detachshader(glprogram->attach[i]); // auto delete if marked as delete!
    }
    deleteProgram(glprogram, k_program);
}

void APIENTRY_GL4ES gl4es_glDetachShader(GLuint program, GLuint shader) {
    DBG(SHUT_LOGD("glDetachShader(%d, %d)\n", program, shader))
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program)
    CHECK_SHADER(void, shader)
    // is shader attached?
    int f = 0;
    while (f < glprogram->attach_size && glprogram->attach[f] != shader)
        f++;
    if (f == glprogram->attach_size) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (f + 1 < glprogram->attach_size) {
        memmove(&glprogram->attach[f], &glprogram->attach[f + 1],
                (size_t)(glprogram->attach_size - f - 1) * sizeof(GLuint));
    }
    glprogram->attach_size--;

    if (glprogram->last_vert == glshader) glprogram->last_vert = NULL;
    if (glprogram->last_frag == glshader) glprogram->last_frag = NULL;
    if (glprogram->last_comp == glshader) glprogram->last_comp = NULL;

    // Only call hardware detach if currently attached on the GLES program object
    if (glprogram->gles_attached) {
        LOAD_GLES2(glDetachShader);
        if (gles_glDetachShader) {
            gles_glDetachShader(glprogram->id, glshader->id);
            errorGL();
        } else {
            noerrorShim();
        }
    } else {
        noerrorShim();
    }
    // marked as detached (frees shader_t + calls gles_glDeleteShader if marked deleted)
    actually_detachshader(shader);
}

void APIENTRY_GL4ES gl4es_glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size,
                                            GLenum* type, GLchar* name) {
    DBG(SHUT_LOGD("glGetActiveAttrib(%d, %d, %d, %p, %p, %p, %p)\n", program, index, bufSize, length, size, type, name))
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program)

    if (glprogram->attribloc) {
        attribloc_t* attribloc;
        kh_foreach_value(
            glprogram->attribloc, attribloc, if (attribloc->real_index == index) {
                if (type) *type = attribloc->type;
                if (size) *size = attribloc->size;
                if (length) *length = strlen(attribloc->glname);
                if (bufSize && name) {
                    strncpy(name, attribloc->glname, bufSize - 1);
                    name[bufSize - 1] = '\0';
                }
                DBG(SHUT_LOGD("found, type=%s, size=%d, name=%s/%s\n", PrintEnum(attribloc->type), attribloc->size,
                              attribloc->name, attribloc->glname))
                noerrorShim();
                return;
            });
    }
    DBG(SHUT_LOGD("not found\n"))
    errorShim(GL_INVALID_VALUE);
}

void APIENTRY_GL4ES gl4es_glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count, GLuint* shaders) {
    DBG(SHUT_LOGD("glGetAttachedShaders(%d, %d, %p, %p)\n", program, maxCount, count, shaders))
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program)

    int n = glprogram->attach_size;
    if (n > maxCount) n = maxCount;

    if (count) *count = n;
    if (shaders)
        for (int i = 0; i < n; i++)
            shaders[i] = glprogram->attach[i];

    noerrorShim();
}

GLint APIENTRY_GL4ES gl4es_glGetAttribLocation(GLuint program, const GLchar* name) {
    DBG(SHUT_LOGD("glGetAttribLocation(%d, %s)\n", program, name));
    FLUSH_BEGINEND;
    CHECK_PROGRAM(GLint, program);

    if (!glprogram->linked) {
        errorShim(GL_INVALID_OPERATION);
        DBG(SHUT_LOGD(" program unlinked: -1\n"));
        return -1;
    }
    noerrorShim();
    if (strncmp(name, "gl_", 3) == 0) {
        DBG(SHUT_LOGD(" internal attrib: -1\n"));
        return -1;
    }

    int rloc = -1;
    int loc = -1;
    // look in already created attribloc
    if (glprogram->attribloc) {
        attribloc_t* m;
        kh_foreach_value(
            glprogram->attribloc, m, if (strcmp(m->glname, name) == 0) {
                loc = m->index;
                rloc = m->real_index;
            })
    }
    // if found, just return the value, it's done...
    if (loc != -1) {
        DBG(SHUT_LOGD(" found in cache: %d\n", loc))
        return loc;
    }
    // end
    DBG(SHUT_LOGD(" asked hardware: %d\n", loc))
    return loc;
}

void APIENTRY_GL4ES gl4es_glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length,
                                             GLint* size, GLenum* type, GLchar* name) {
    DBG(SHUT_LOGD("glGetActiveUniform(%d, %d, %d, %p, %p, %p, %p)\n", program, index, bufSize, length, size, type,
                  name))
    FLUSH_BEGINEND;
    CHECK_PROGRAM(GLvoid, program);

    if (!glprogram->linked) {
        errorShim(GL_INVALID_OPERATION);
        DBG(SHUT_LOGD(" not linked\n"))
        return;
    }
    if (index >= (GLuint)glprogram->num_uniform) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    noerrorShim();

    // look in uniform cache, that is filled when program is linked
    if (glprogram->uniform) {
        uniform_t* m;
        kh_foreach_value(
            glprogram->uniform, m, if (m->internal_id == index) {
                if (type) *type = m->type;
                if (size) *size = m->size;
                if (length) *length = strlen(m->name);
                if (bufSize && name) {
                    strncpy(name, m->name, bufSize - 1);
                    name[bufSize - 1] = '\0';
                }
                DBG(SHUT_LOGD(" found %s (%zd), type=%s, size=%d\n", m->name, strlen(m->name), PrintEnum(m->type),
                              m->size))
                return;
            });
    }
    // end
    DBG(SHUT_LOGD(" not found\n"))
    errorShim(GL_INVALID_VALUE);
}

static const char* notlinked = "Program not linked";
static const char* linked = "Program linked, but no shader support";
static const char* validated = "Program validated, but no shader support";
const char* getFakeProgramInfo(program_t* glprogram) {
    return (glprogram->linked) ? ((glprogram->validated) ? validated : linked) : notlinked;
}

void APIENTRY_GL4ES gl4es_glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog) {
    DBG(SHUT_LOGD("glGetProgramInfoLog(%d, %d, %p, %p)\n", program, maxLength, length, infoLog))
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program)

    if (maxLength < 0) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (!maxLength) {
        noerrorShim();
        return;
    }

    LOAD_GLES2(glGetProgramInfoLog);
    if (gles_glGetProgramInfoLog) {
        gles_glGetProgramInfoLog(glprogram->id, maxLength, length, infoLog);
        errorGL();
    } else {
        const char* res = getFakeProgramInfo(glprogram);
        int l = strlen(res) + 1;
        if (l > maxLength) l = maxLength;
        if (length) *length = l - 1;
        if (infoLog) strncpy(infoLog, res, l);
        noerrorShim();
    }
}

void APIENTRY_GL4ES gl4es_glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    DBG(SHUT_LOGD("glGetProgramiv(%d, %s, %p)\n", program, PrintEnum(pname), params))
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program)

    LOAD_GLES2(glGetProgramiv);
    noerrorShim();
    switch (pname) {
    case GL_DELETE_STATUS:
        if (gles_glGetProgramiv) {
            gles_glGetProgramiv(glprogram->id, pname, params);
            errorGL();
        } else
            *params = GL_FALSE;
        break;
    case GL_LINK_STATUS:
        *params = glprogram->linked ? GL_TRUE : GL_FALSE;
        break;
    case GL_VALIDATE_STATUS:
        *params = glprogram->valid_result;
        break;
    case GL_INFO_LOG_LENGTH:
        if (gles_glGetProgramiv) {
            gles_glGetProgramiv(glprogram->id, pname, params);
            errorGL();
        } else
            *params = strlen(getFakeProgramInfo(glprogram));
        break;
    case GL_ATTACHED_SHADERS:
        *params = glprogram->attach_size;
        break;
    case GL_ACTIVE_ATTRIBUTES:
    case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
        if (gles_glGetProgramiv) {
            gles_glGetProgramiv(glprogram->id, pname, params);
            errorGL();
        } else
            *params = 0;
        break;
    case GL_ACTIVE_UNIFORMS:
        *params = (glprogram->uniform) ? glprogram->num_uniform : 0;
        break;
    case GL_ACTIVE_UNIFORM_MAX_LENGTH: {
        int l = 0;
        if (glprogram->uniform) {
            uniform_t* m;
            kh_foreach_value(glprogram->uniform, m,
                             if (m->internal_id != ~0u && l < (int)strlen(m->name) + 1) l = (int)strlen(m->name) + 1;)
        }
        *params = l;
    } break;
    case GL_PROGRAM_BINARY_LENGTH:
        // TODO: check if extension is present
        if (gles_glGetProgramiv) {
            gles_glGetProgramiv(glprogram->id, pname, params);
            errorGL();
        } else
            errorShim(GL_INVALID_ENUM);
        break;

    default:
        if (gles_glGetProgramiv) {
            gles_glGetProgramiv(glprogram->id, pname, params);
            errorGL();
        } else
            errorShim(GL_INVALID_ENUM);
        break;
    }
}

GLint APIENTRY_GL4ES gl4es_glGetUniformLocation(GLuint program, const GLchar* name) {
    DBG(SHUT_LOGD("glGetUniformLocation(%d, %s)\n", program, name))
    FLUSH_BEGINEND;
    CHECK_PROGRAM(GLint, program)

    noerrorShim();
    if (!name || !name[0]) return -1;
    if (name[0] == 'g' && name[1] == 'l' && name[2] == '_') {
        DBG(SHUT_LOGD(" internal uniform: -1\n"))
        return -1;
    }
    if (strncmp(name, "vgpu_texture", 12) == 0 &&
        (name[12] == '\0' || name[12] == '[' || name[12] == '.')) {
        name += 5; // "vgpu_texture..." -> "texture..."
    } else if (strcmp(name, "vgpu_Sample") == 0) {
        name = "sample";
    }

    /* O(1) hash lookup for both scalar ("gbufferModelViewInverse") and array ("arr[3]") uniforms */
    if (glprogram->uniform_by_name) {
        khint_t k = kh_get(uniformnamelist, glprogram->uniform_by_name, name);
        if (k != kh_end(glprogram->uniform_by_name)) {
            return kh_value(glprogram->uniform_by_name, k);
        }
        /* Handle "arr[0]" alias -> base "arr" */
        size_t l = strlen(name);
        if (l > 3 && name[l - 3] == '[' && name[l - 2] == '0' && name[l - 1] == ']') {
            char stack_buf[256];
            size_t base_len = l - 3;
            char* base_name = (base_len < sizeof(stack_buf)) ? stack_buf : (char*)malloc(base_len + 1);
            if (base_name) {
                memcpy(base_name, name, base_len);
                base_name[base_len] = '\0';
                k = kh_get(uniformnamelist, glprogram->uniform_by_name, base_name);
                if (base_name != stack_buf) free(base_name);
                if (k != kh_end(glprogram->uniform_by_name)) {
                    return kh_value(glprogram->uniform_by_name, k);
                }
            }
        }
    }

    int res = -1;
    int index = 0;
    int l = (int)strlen(name);
    if (l > 0 && name[l - 1] == ']') {
        const char* p = strrchr(name, '[');
        if (p) {
            l = (int)(p - name);
            p++;
            while (*p >= '0' && *p <= '9') {
                index = index * 10 + (*(p++) - '0');
            }
        }
    }
    if (glprogram->uniform) {
        uniform_t* m;
        kh_foreach_value(
            glprogram->uniform, m,
            if ((int)strlen(m->name) == l && strncmp(m->name, name, (size_t)l) == 0) {
                res = (GLint)m->id;
                if (index >= m->size) {
                    res = -1;
                } else {
                    res += index;
                }
                break;
            })
    }
    DBG(SHUT_LOGD(" location: %d\n", res))
    return res;
}

GLboolean APIENTRY_GL4ES gl4es_glIsProgram(GLuint program) {
    DBG(SHUT_LOGD("glIsProgram(%d)\n", program))
    FLUSH_BEGINEND;
    noerrorShim();
    if (!program) {
        return GL_FALSE;
    }
    program_t* glprogram = NULL;
    khint_t k;
    khash_t(programlist)* programs = glstate->glsl->programs;
    k = kh_get(programlist, programs, program);
    if (k != kh_end(programs)) return GL_TRUE;
    return GL_FALSE;
}

static void clear_program(program_t* glprogram) {
    // clear all Attrib location cache
    if (glprogram->attribloc) {
        attribloc_t* m;
        khint_t k;
        int ret;
        // attribloc->glname must not be freed
        kh_foreach(glprogram->attribloc, k, m, free(m->name); free(m); kh_del(attribloclist, glprogram->attribloc, k);)
            kh_destroy(attribloclist, glprogram->attribloc);
        khash_t(attribloclist)* attribloc = glprogram->attribloc = kh_init(attribloclist);
        kh_put(attribloclist, attribloc, 1, &ret);
        kh_del(attribloclist, attribloc, 1);
    }
    // clear all Uniform cache
    glprogram->num_uniform = 0;
    if (glprogram->uniform_by_name) {
        kh_clear(uniformnamelist, glprogram->uniform_by_name);
    } else {
        glprogram->uniform_by_name = kh_init(uniformnamelist);
    }
    if (glprogram->uniform) {
        uniform_t* m;
        khint_t k;
        kh_foreach(glprogram->uniform, k, m, free(m->name); free(m); kh_del(uniformlist, glprogram->uniform, k);)
    }
    if (glprogram->parent_sync_list) {
        free(glprogram->parent_sync_list);
        glprogram->parent_sync_list = NULL;
    }
    glprogram->parent_sync_count = 0;
    glprogram->pending_count = 0;
    if (glprogram->custom_uniforms) {
        free(glprogram->custom_uniforms);
        glprogram->custom_uniforms = NULL;
    }
    glprogram->custom_uniform_count = 0;
    glprogram->synced_sm_gen = 0;
    glprogram->uniform_gen = 1;
    glprogram->synced_parent_gen = 0;
    memset(glprogram->va_size, 0, sizeof(glprogram->va_size));
    glprogram->va_mask = 0;
    glprogram->cache.size = 0; // reset cache buffer
}

static void fill_program(program_t* glprogram) {
    LOAD_GLES(glGetError);
    LOAD_GLES2(glGetProgramiv);
    LOAD_GLES2(glGetActiveUniform);
    LOAD_GLES2(glGetUniformLocation);
    LOAD_GLES2(glGetActiveAttrib);
    LOAD_GLES2(glGetAttribLocation);
    int n = 0;
    int maxsize = 0;
    khint_t k;
    int ret;
    DBG(GLenum e2);

    // init bluitin emulation first
    builtin_Init(glprogram);
    // Grab all Uniform
    gles_glGetProgramiv(glprogram->id, GL_ACTIVE_UNIFORMS, &n);
    gles_glGetProgramiv(glprogram->id, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxsize);
    int u_buf_sz = (maxsize < 256) ? 256 : maxsize;
    khash_t(uniformlist)* uniforms = glprogram->uniform;
    uniform_t* gluniform = NULL;
    int uniform_cache = 0;
    GLint size = 0;
    GLenum type = 0;
    GLchar* name = (char*)malloc((size_t)u_buf_sz);
    int tu_idx = 0;
    int custom_cap = 0;
    for (int i = 0; i < n && name; i++) {
        name[0] = '\0';
        size = 0;
        type = 0;
        gles_glGetActiveUniform(glprogram->id, i, u_buf_sz, NULL, &size, &type, name);
        name[u_buf_sz - 1] = '\0';
        DBG(e2 = gles_glGetError());
        DBG(if (e2 == GL_NO_ERROR)) {
            // remove any ending "[]" that could be present
            size_t nlen = strlen(name);
            if (nlen > 0 && name[nlen - 1] == ']' && strrchr(name, '[')) (*strrchr(name, '[')) = '\0';
            GLint id = gles_glGetUniformLocation(glprogram->id, name);
            if (id != -1) {
                /* Unmap vgpu_texture / vgpu_Sample back to original GLSL 120 names ("texture" / "sample")
                   so Iris ProgramSamplers / OptiFine Shaders / GL20.glGetUniformLocation find them */
                if (strncmp(name, "vgpu_texture", 12) == 0 &&
                    (name[12] == '\0' || name[12] == '[' || name[12] == '.')) {
                    memmove(name, name + 5, strlen(name + 5) + 1);
                } else if (strncmp(name, "vgpu_Sample", 11) == 0 &&
                           (name[11] == '\0' || name[11] == '[' || name[11] == '.')) {
                    name[5] = 's';
                    memmove(name, name + 5, strlen(name + 5) + 1);
                }
                for (int j = 0; j < size; j++) {
                    k = kh_put(uniformlist, uniforms, id, &ret);
                    gluniform = kh_value(uniforms, k) = malloc(sizeof(uniform_t));
                    memset(gluniform, 0, sizeof(uniform_t));
                    if (j) {
                        gluniform->name = malloc(strlen(name) + 1 + 8);
                        sprintf(gluniform->name, "%s[%d]", name, j);
                    } else
                        gluniform->name = strdup(name);
                    gluniform->id = id;
                    if (glprogram->uniform_by_name && gluniform->name) {
                        int nret = 0;
                        khint_t nk = kh_put(uniformnamelist, glprogram->uniform_by_name, gluniform->name, &nret);
                        kh_value(glprogram->uniform_by_name, nk) = id;
                    }
                    gluniform->size = size - j;
                    gluniform->type = type;
                    gluniform->cache_offs = uniform_cache + j * uniformsize(type);
                    gluniform->cache_size = uniformsize(type) * (size - j);
                    gluniform->builtin = builtin_CheckUniform(glprogram, name, id, size - j);
                    /* Hide internal _gl4es_* builtins and secondary array elements from app reflection */
                    if (j == 0 && !gluniform->builtin && gluniform->name &&
                        strncmp(gluniform->name, "_gl4es_", 7) != 0) {
                        gluniform->internal_id = (GLuint)(glprogram->num_uniform++);
                        gluniform->sm_source_id = emesis_sm_classify_source(gluniform->name);
                        gluniform->sm_custom_id = emesis_sm_classify_custom(gluniform->name, type);
                        if (gluniform->sm_custom_id != 0) {
                            if (glprogram->custom_uniform_count >= custom_cap) {
                                int ncap = custom_cap ? (custom_cap * 2) : 8;
                                uniform_t** nc = (uniform_t**)realloc(glprogram->custom_uniforms,
                                                                      (size_t)ncap * sizeof(uniform_t*));
                                if (nc) {
                                    glprogram->custom_uniforms = nc;
                                    custom_cap = ncap;
                                }
                            }
                            if (glprogram->custom_uniform_count < custom_cap) {
                                glprogram->custom_uniforms[glprogram->custom_uniform_count++] = gluniform;
                            }
                        }
                    } else {
                        gluniform->internal_id = ~0u;
                    }
                    // TextureUnit grabbing...
                    if (tu_idx < MAX_TEX_UNITS) {
                        if (type == GL_SAMPLER_CUBE) {
                            glprogram->texunits[tu_idx].id = id;
                            glprogram->texunits[tu_idx].type = TU_CUBE;
                            glprogram->texunits[tu_idx].req_tu = glprogram->texunits[tu_idx].act_tu = 0;
                            ++tu_idx;
                        } else if (type == GL_SAMPLER_2D || type == GL_INT_SAMPLER_2D ||
                                   type == GL_UNSIGNED_INT_SAMPLER_2D) {
                            glprogram->texunits[tu_idx].id = id;
                            glprogram->texunits[tu_idx].type = TU_TEX2D;
                            glprogram->texunits[tu_idx].req_tu = glprogram->texunits[tu_idx].act_tu = 0;
                            ++tu_idx;
                        } else if (type == GL_SAMPLER_2D_SHADOW) {
                            glprogram->texunits[tu_idx].id = id;
                            glprogram->texunits[tu_idx].type = TU_TEX2DSH;
                            glprogram->texunits[tu_idx].req_tu = glprogram->texunits[tu_idx].act_tu = 0;
                            ++tu_idx;
                        } else if (type == GL_SAMPLER_3D || type == GL_INT_SAMPLER_3D ||
                                   type == GL_UNSIGNED_INT_SAMPLER_3D) {
                            glprogram->texunits[tu_idx].id = id;
                            glprogram->texunits[tu_idx].type = TU_TEX3D;
                            glprogram->texunits[tu_idx].req_tu = glprogram->texunits[tu_idx].act_tu = 0;
                            ++tu_idx;
                        }
                    }
                    DBG(SHUT_LOGD(" uniform #%d : \"%s\"%s type=%s size=%d\n", id, gluniform->name,
                                  gluniform->builtin ? " (builtin) " : "", PrintEnum(gluniform->type), gluniform->size))
                    id++;
                }
                uniform_cache += uniformsize(type) * size;
            }
        }
        DBG(else SHUT_LOGD("LIBGL: Warning, getting Uniform #%d info failed with %s\n", i, PrintEnum(e2)))
    }
    free(name);
    // reset uniform cache
    if (glprogram->cache.cap < uniform_cache) {
        glprogram->cache.cap = uniform_cache;
        glprogram->cache.cache = realloc(glprogram->cache.cache, glprogram->cache.cap);
    }
    if (glprogram->cache.cache && glprogram->cache.cap > 0) {
        memset(glprogram->cache.cache, 0, glprogram->cache.cap);
    }
    // Initialize all sampler & image uniform cache slots to 0xff (-1) so initial glUniform1i(loc, 0) is never skipped
    if (glprogram->uniform) {
        uniform_t* m;
        khint_t k;
        kh_foreach(glprogram->uniform, k, m,
                   if ((m->type >= 0x8B5D && m->type <= 0x8B64) ||
                       (m->type >= 0x8DC0 && m->type <= 0x8DDD) ||
                       (m->type >= 0x900C && m->type <= 0x900F) ||
                       (m->type >= 0x904C && m->type <= 0x906C) ||
                       (m->type >= 0x9108 && m->type <= 0x910D))
                       memset((char*)glprogram->cache.cache + m->cache_offs, 0xff, m->cache_size);)
    }

    // Grab all Attrib
    gles_glGetProgramiv(glprogram->id, GL_ACTIVE_ATTRIBUTES, &n);
    gles_glGetProgramiv(glprogram->id, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &maxsize);
    int a_buf_sz = (maxsize < 256) ? 256 : maxsize;
    name = (char*)malloc((size_t)a_buf_sz);
    for (int i = 0; i < n && name; i++) {
        DBG(e2 = gles_glGetError());
        DBG(if (e2 == GL_NO_ERROR)) {
            name[0] = '\0';
            gles_glGetActiveAttrib(glprogram->id, i, a_buf_sz, NULL, &size, &type, name);
            name[a_buf_sz - 1] = '\0';
            GLint id = gles_glGetAttribLocation(glprogram->id, name);
            if (id != -1) {
                attribloc_t* glattribloc = NULL;
                k = kh_put(attribloclist, glprogram->attribloc, id, &ret);
                if (ret == 0) {
                    // already there
                    glattribloc = kh_value(glprogram->attribloc, k);
                    if (glattribloc->name) free(glattribloc->name);
                } else {
                    glattribloc = kh_value(glprogram->attribloc, k) = malloc(sizeof(attribloc_t));
                }
                memset(glattribloc, 0, sizeof(attribloc_t));
                glattribloc->name = strdup(name);
                if (strcmp(glattribloc->name, "vgpu_texture") == 0) {
                    glattribloc->glname = "texture";
                } else if (strcmp(glattribloc->name, "vgpu_Sample") == 0) {
                    glattribloc->glname = "sample";
                } else {
                    glattribloc->glname = builtinAttribGLName(glattribloc->name);
                }
                glattribloc->size = size;
                glattribloc->type = type;
                glattribloc->index = id;
                glattribloc->real_index = i;
                int builtin = builtin_CheckVertexAttrib(glprogram, name, id);
                glprogram->va_size[id] = n_uniform(type); // same as uniform
                if (id >= 0 && id < 32 && glprogram->va_size[id]) {
                    glprogram->va_mask |= (1u << (unsigned)id);
                }
                DBG(SHUT_LOGD(" attrib #%d : \"%s\"%s type=%s size=%d\n", id, glattribloc->name,
                              builtin ? " (builtin) " : "", PrintEnum(glattribloc->type), glattribloc->size))
            }
        }
        DBG(else SHUT_LOGD("LIBGL: Warning, getting Attrib #%d info failed with %s\n", i, PrintEnum(e2)))
    }
    free(name);
}

static int is_external_gles_fn_prog(const void* cand, const void* self_anchor) {
    if (!cand || cand == self_anchor) return 0;
    Dl_info self_info, cand_info;
    if (dladdr(self_anchor, &self_info) && dladdr(cand, &cand_info)) {
        if (cand_info.dli_fbase && cand_info.dli_fbase == self_info.dli_fbase) return 0;
        if (cand_info.dli_fname && strstr(cand_info.dli_fname, "emesis") != NULL) return 0;
    }
    return 1;
}

static glProgramBinary_PTR resolve_program_binary_fn(void) {
    static glProgramBinary_PTR fn = NULL;
    static int checked = 0;
    if (!checked) {
        checked = 1;
        const char* names[2] = {"glProgramBinary", "glProgramBinaryOES"};
        for (int i = 0; i < 2 && !fn; ++i) {
            if (gles) {
                void* p = proc_address(gles, names[i]);
                if (is_external_gles_fn_prog(p, (const void*)&resolve_program_binary_fn)) {
                    fn = (glProgramBinary_PTR)p;
                    break;
                }
            }
        }
#ifndef NOEGL
        if (!fn) {
            LOAD_EGL(eglGetProcAddress);
            if (egl_eglGetProcAddress) {
                for (int i = 0; i < 2 && !fn; ++i) {
                    void* p = (void*)egl_eglGetProcAddress(names[i]);
                    if (is_external_gles_fn_prog(p, (const void*)&resolve_program_binary_fn)) {
                        fn = (glProgramBinary_PTR)p;
                        break;
                    }
                }
            }
        }
#endif
    }
    return fn;
}

static glGetProgramBinary_PTR resolve_get_program_binary_fn(void) {
    static glGetProgramBinary_PTR fn = NULL;
    static int checked = 0;
    if (!checked) {
        checked = 1;
        const char* names[2] = {"glGetProgramBinary", "glGetProgramBinaryOES"};
        for (int i = 0; i < 2 && !fn; ++i) {
            if (gles) {
                void* p = proc_address(gles, names[i]);
                if (is_external_gles_fn_prog(p, (const void*)&resolve_get_program_binary_fn)) {
                    fn = (glGetProgramBinary_PTR)p;
                    break;
                }
            }
        }
#ifndef NOEGL
        if (!fn) {
            LOAD_EGL(eglGetProcAddress);
            if (egl_eglGetProcAddress) {
                for (int i = 0; i < 2 && !fn; ++i) {
                    void* p = (void*)egl_eglGetProcAddress(names[i]);
                    if (is_external_gles_fn_prog(p, (const void*)&resolve_get_program_binary_fn)) {
                        fn = (glGetProgramBinary_PTR)p;
                        break;
                    }
                }
            }
        }
#endif
    }
    return fn;
}

int gl4es_useProgramBinary(GLuint program, int length, GLenum format, const void* binary) {
    if (hardext.prgbin_n == 0) return 0;
    DBG(SHUT_LOGD("useProgramBinary(%d, %d, %d, %p)\n", program, length, format, binary))
    CHECK_PROGRAM(int, program)
    noerrorShim();

    glProgramBinary_PTR fn_prog_bin = resolve_program_binary_fn();
    LOAD_GLES2(glGetProgramiv);
    if (!fn_prog_bin || !gles_glGetProgramiv) return 0;

    clear_program(glprogram);

    fn_prog_bin(glprogram->id, format, binary, length);

    gles_glGetProgramiv(glprogram->id, GL_LINK_STATUS, &glprogram->linked);
    DBG(SHUT_LOGD(" link status = %d\n", glprogram->linked))
    if (glprogram->linked) {
        fill_program(glprogram);
    } else {
        // should DBG the linker error?
        DBG(SHUT_LOGD(" Load failed!\n"))
        glprogram->linked = 0;
        errorGL();
    }
    return glprogram->linked;
}

int gl4es_getProgramBinary(GLuint program, int* length, GLenum* format, void** binary) {
    if (hardext.prgbin_n == 0 || !length || !format || !binary) return 0;
    DBG(SHUT_LOGD("getProgramBinary(%d, %p, %p, %p)\n", program, length, format, binary))
    CHECK_PROGRAM(int, program)
    noerrorShim();

    glGetProgramBinary_PTR fn_get_prog_bin = resolve_get_program_binary_fn();
    LOAD_GLES2(glGetProgramiv);
    if (!fn_get_prog_bin || !gles_glGetProgramiv) return 0;

    int l = 0;
    gles_glGetProgramiv(glprogram->id, GL_PROGRAM_BINARY_LENGTH_OES, &l);
    if (l <= 0) return 0;

    *binary = malloc(l);
    if (!*binary) return 0;
    *length = 0;
    fn_get_prog_bin(glprogram->id, l, length, format, *binary);
    if (*length <= 0) {
        free(*binary);
        *binary = NULL;
        return 0;
    }

    return (*length);
}

void APIENTRY_GL4ES gl4es_glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei* length, GLenum* binaryFormat,
                                             void* binary) {
    if (hardext.prgbin_n == 0) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    DBG(SHUT_LOGD("glGetProgramBinary(%d, %d, %p, %p, %p)\n", program, bufSize, length, binaryFormat, binary))
    CHECK_PROGRAM(void, program)
    glGetProgramBinary_PTR fn_get_prog_bin = resolve_get_program_binary_fn();
    if (!fn_get_prog_bin) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    fn_get_prog_bin(glprogram->id, bufSize, length, binaryFormat, binary);
    errorGL();
}

void APIENTRY_GL4ES gl4es_glProgramBinary(GLuint program, GLenum binaryFormat, const void* binary, GLsizei length) {
    if (hardext.prgbin_n == 0) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    DBG(SHUT_LOGD("glProgramBinary(%d, %d, %p, %d)\n", program, binaryFormat, binary, length))
    if (gl4es_useProgramBinary(program, length, binaryFormat, binary))
        noerrorShim();
    else
        errorShim(GL_INVALID_OPERATION);
}

static void apply_program_uniform_defaults(GLuint program, program_t* glprogram) {
    /* Re-merge declarations from all attached shaders in case glShaderSource or redoShader ran after glAttachShader */
    for (int i = 0; i < glprogram->attach_size; ++i) {
        shader_t* sh = getShader(glprogram->attach[i]);
        if (sh) {
            uniform_decls_merge(&glprogram->declarations, &sh->uniforms_declarations);
        }
    }
    if (glprogram->declarations.count <= 0) return;

    GLuint prev_prog = glstate->glsl->program;
    program_t* prev_glprog = glstate->glsl->glprogram;
    int prev_internal = g_emesis_internal_uniform_upload;
    glstate->glsl->program = glprogram->id;
    glstate->glsl->glprogram = glprogram;
    g_emesis_internal_uniform_upload = 1;

    set_uniforms_default_value(program, &glprogram->declarations);

    g_emesis_internal_uniform_upload = prev_internal;
    glstate->glsl->program = prev_prog;
    glstate->glsl->glprogram = prev_glprog;
}

static char* serialize_attrib_bindings(program_t* glprogram) {
    if (!glprogram || !glprogram->attribloc || kh_size(glprogram->attribloc) == 0) {
        return NULL;
    }
    size_t cap = 256;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    attribloc_t* m;
    kh_foreach_value(glprogram->attribloc, m, {
        if (m && m->name) {
            size_t need = strlen(m->name) + 24;
            if (len + need >= cap) {
                cap = (cap + need) * 2;
                char* nb = (char*)realloc(buf, cap);
                if (!nb) break;
                buf = nb;
            }
            int w = snprintf(buf + len, cap - len, "%u:%s;", m->index, m->name);
            if (w > 0) len += (size_t)w;
        }
    });
    return buf;
}

void APIENTRY_GL4ES gl4es_glLinkProgram(GLuint program) {
    DBG(SHUT_LOGD("glLinkProgram(%d)\n", program))
    FLUSH_BEGINEND;
    CHECK_PROGRAM(void, program)
    noerrorShim();

    /* Capture caller's explicit glBindAttribLocation bindings BEFORE clear_program resets glprogram->attribloc */
    char* caller_attrib_sig = serialize_attrib_bindings(glprogram);

    clear_program(glprogram);

    if (!glprogram->last_comp) {
        shaderconv_need_t needs = {0};
        needs.need_texcoord = -1;
        for (int i = 0; i < glprogram->attach_size; i++) {
            accumShaderNeeds(glprogram->attach[i], &needs);
        }
        if (!glprogram->last_vert) {
            if (glprogram->default_need) free(glprogram->default_need);
            glprogram->default_need = (shaderconv_need_t*)malloc(sizeof(shaderconv_need_t));
            memcpy(glprogram->default_need, &needs, sizeof(shaderconv_need_t));
            glprogram->default_vertex = 1;
            GLenum vtx = gl4es_glCreateShader(GL_VERTEX_SHADER);
            gl4es_glShaderSource(vtx, 1, fpe_VertexShader(&needs, NULL), NULL);
            gl4es_glCompileShader(vtx);
            gl4es_glAttachShader(glprogram->id, vtx);
            gl4es_glDeleteShader(vtx);
        }
        if (!glprogram->last_frag) {
            if (glprogram->default_need) free(glprogram->default_need);
            glprogram->default_need = (shaderconv_need_t*)malloc(sizeof(shaderconv_need_t));
            memcpy(glprogram->default_need, &needs, sizeof(shaderconv_need_t));
            glprogram->default_fragment = 1;
            GLenum vtx = gl4es_glCreateShader(GL_FRAGMENT_SHADER);
            gl4es_glShaderSource(vtx, 1, fpe_FragmentShader(&needs, NULL), NULL);
            gl4es_glCompileShader(vtx);
            gl4es_glAttachShader(glprogram->id, vtx);
            gl4es_glDeleteShader(vtx);
        }
        int compatible = 1;
        for (int i = 0; i < glprogram->attach_size && compatible; i++) {
            compatible = isShaderCompatible(glprogram->attach[i], &needs);
        }
        if (!compatible) {
            DBG(SHUT_LOGD("Need to redo some shaders...\n"))
            for (int i = 0; i < glprogram->attach_size; i++) {
                if (!isShaderCompatible(glprogram->attach[i], &needs)) {
                    redoShader(glprogram->attach[i], &needs);
                }
            }
        }
        /* Always bind _gl4es_* builtin vertex attributes regardless of translation backend */
        if (glprogram->last_vert && glprogram->last_vert->converted) {
            for (int i = 0; i < ATT_MAX; ++i) {
                const char* attribute = hasBuiltinAttrib(glprogram->last_vert->converted, i);
                if (attribute) gl4es_glBindAttribLocation(glprogram->id, i, attribute);
            }
        }
        if (glprogram->last_vert && glprogram->last_frag && glprogram->frag_data_changed == 1) {
            LOAD_GLES2(glShaderSource);
            LOAD_GLES2(glCompileShader);
            if (gles_glShaderSource && gles_glCompileShader) {
                gles_glShaderSource(glprogram->last_frag->id, 1, (const GLchar* const*)&glprogram->last_frag->converted,
                                    NULL);
                gles_glCompileShader(glprogram->last_frag->id);
                LOAD_GLES2(glGetShaderiv);
                GLint status = 0;
                gles_glGetShaderiv(glprogram->last_frag->id, GL_COMPILE_STATUS, &status);
                if (status != GL_TRUE && glprogram->last_frag->before_patch) {
                    gles_glShaderSource(glprogram->last_frag->id, 1,
                                        (const GLchar* const*)&glprogram->last_frag->before_patch, NULL);
                    gles_glCompileShader(glprogram->last_frag->id);
                }
                glprogram->last_frag->gles_compiled = 1;
            } else {
                noerrorShim();
            }
        }
    }

    const char* vs_essl = (glprogram->last_vert && glprogram->last_vert->converted)
                              ? glprogram->last_vert->converted
                              : (glprogram->last_vert ? glprogram->last_vert->source : NULL);
    const char* fs_essl = (glprogram->last_frag && glprogram->last_frag->converted)
                              ? glprogram->last_frag->converted
                              : (glprogram->last_frag ? glprogram->last_frag->source : NULL);
    const char* cs_essl = (glprogram->last_comp && glprogram->last_comp->converted)
                              ? glprogram->last_comp->converted
                              : (glprogram->last_comp ? glprogram->last_comp->source : NULL);

    if (glprogram->vert_essl_copy) {
        free(glprogram->vert_essl_copy);
        glprogram->vert_essl_copy = NULL;
    }
    if (glprogram->frag_essl_copy) {
        free(glprogram->frag_essl_copy);
        glprogram->frag_essl_copy = NULL;
    }
    if (vs_essl) glprogram->vert_essl_copy = strdup(vs_essl);
    if (fs_essl) glprogram->frag_essl_copy = strdup(fs_essl);

    /* Fast path: load pre-linked native GPU program binary from RAM/disk cache (.ep1) */
    if (!glprogram->frag_data_changed &&
        emesis_program_cache_try_load(glprogram->id, vs_essl, fs_essl, cs_essl, caller_attrib_sig)) {
        free(caller_attrib_sig);
        glprogram->linked = 1;
        fill_program(glprogram);
        apply_program_uniform_defaults(program, glprogram);
        noerrorShimNoPurge();
        return;
    }

    /* .ep1 cache miss: compile and attach shaders in the GLES driver before linking */
    LOAD_GLES2(glAttachShader);
    LOAD_GLES2(glDetachShader);
    for (int i = 0; i < glprogram->attach_size; ++i) {
        shader_t* sh = getShader(glprogram->attach[i]);
        ensure_shader_gles_compiled(sh);
        if (sh && gles_glAttachShader) {
            gles_glAttachShader(glprogram->id, sh->id);
        }
    }
    glprogram->gles_attached = 1;

    LOAD_GLES2(glLinkProgram);
    if (gles_glLinkProgram) {
        LOAD_GLES(glGetError);
        LOAD_GLES2(glGetProgramiv);
        if (hardext.esversion >= 3 && hardext.prgbin_n > 0 && !glprogram->frag_data_changed) {
            typedef void (*glProgramParameteri_fn)(GLuint, GLenum, GLint);
            static glProgramParameteri_fn fn_prog_param = NULL;
            static int checked_prog_param = 0;
            if (!checked_prog_param) {
                checked_prog_param = 1;
                if (gles) fn_prog_param = (glProgramParameteri_fn)proc_address(gles, "glProgramParameteri");
            }
            if (fn_prog_param) {
                fn_prog_param(glprogram->id, 0x8257 /* GL_PROGRAM_BINARY_RETRIEVABLE_HINT */, GL_TRUE);
                while (gles_glGetError() != GL_NO_ERROR) {}
            }
        }
        gles_glLinkProgram(glprogram->id);
        GLenum err = gles_glGetError();
        gles_glGetProgramiv(glprogram->id, GL_LINK_STATUS, &glprogram->linked);

        if (glprogram->linked && !glprogram->frag_data_changed) {
            emesis_program_cache_store(glprogram->id, vs_essl, fs_essl, cs_essl, caller_attrib_sig);
        }

        /* Detach GLES shader objects right after linking so the GLES driver can immediately
           release internal compiler AST/IR memory when shaders are deleted */
        if (gles_glDetachShader) {
            for (int i = 0; i < glprogram->attach_size; ++i) {
                shader_t* sh = getShader(glprogram->attach[i]);
                if (sh) gles_glDetachShader(glprogram->id, sh->id);
            }
            while (gles_glGetError() != GL_NO_ERROR) {}
        }
        glprogram->gles_attached = 0;

        {
            static uint32_t s_full_links = 0;
            if ((++s_full_links & 7u) == 0u) {
                LOAD_GLES2(glReleaseShaderCompiler);
                if (gles_glReleaseShaderCompiler) {
                    gles_glReleaseShaderCompiler();
                }
            }
        }

        DBG(SHUT_LOGD(" link status = %d\n", glprogram->linked))
        if (glprogram->linked) {
            free(caller_attrib_sig);
            fill_program(glprogram);
            apply_program_uniform_defaults(program, glprogram);
            noerrorShimNoPurge();
        } else {
            free(caller_attrib_sig);
            GLsizei log_length = 0;
            gles_glGetProgramiv(glprogram->id, GL_INFO_LOG_LENGTH, &log_length);
            if (log_length > 0) {
                LOAD_GLES2(glGetProgramInfoLog);
                GLchar* log_chars = (GLchar*)malloc((size_t)log_length + 1);
                if (log_chars) {
                    gles_glGetProgramInfoLog(glprogram->id, log_length, &log_length, log_chars);
                    DBG(SHUT_LOGD("%s", log_chars));
                    free(log_chars);
                }
            }
            glprogram->linked = 0;
            errorShim(err);
            return;
        }
    } else {
        free(caller_attrib_sig);
        noerrorShim();
    }
    glprogram->linked = 1;
}

void APIENTRY_GL4ES gl4es_glUseProgram(GLuint program) {
    DBG(SHUT_LOGD("glUseProgram(%d) old=%d\n", program, glstate->glsl->program))
    PUSH_IF_COMPILING(glUseProgram);
    if (program == 0) {
        glstate->glsl->program = 0;
        glstate->glsl->glprogram = NULL;
        return;
    }
    CHECK_PROGRAM(void, program)
    noerrorShim();
    DBG(SHUT_LOGD("program id=%d\n", glprogram->id))

    glstate->glsl->program = glprogram->id;
    glstate->glsl->glprogram = glprogram;
}

void APIENTRY_GL4ES gl4es_glValidateProgram(GLuint program) {
    CHECK_PROGRAM(void, program)
    FLUSH_BEGINEND;
    noerrorShim();

    LOAD_GLES2(glValidateProgram);
    if (gles_glValidateProgram) {
        LOAD_GLES(glGetError);
        LOAD_GLES2(glGetProgramiv);
        gles_glValidateProgram(glprogram->id);
        GLenum err = gles_glGetError();
        gles_glGetProgramiv(glprogram->id, GL_VALIDATE_STATUS, (GLint*)&glprogram->valid_result);
        errorShim(err);
        // TODO: grab all Uniform and Attrib of the program
    } else {
        noerrorShim();
    }
    glprogram->validated = 1;
}

// ============== GL_ARB_shader_objects ==================
// some are not simple alias but wrappers
GLvoid APIENTRY_GL4ES gl4es_glDeleteObject(GLhandleARB obj) {
    if (!obj) return;
    // find type of obj (shader or program)
    program_t* glprogram = NULL;
    khint_t k_program;
    {
        khash_t(programlist)* programs = glstate->glsl->programs;
        k_program = kh_get(programlist, programs, obj);
        if (k_program != kh_end(programs)) glprogram = kh_value(programs, k_program);
    }
    if (glprogram)
        gl4es_glDeleteProgram(obj);
    else
        gl4es_glDeleteShader(obj);
}

GLhandleARB APIENTRY_GL4ES gl4es_glGetHandle(GLenum pname) {
    FLUSH_BEGINEND;
    if (pname != GL_PROGRAM_OBJECT_ARB) {
        errorShim(GL_INVALID_ENUM);
        return 0;
    }
    return glstate->glsl->program;
}

GLvoid APIENTRY_GL4ES gl4es_glDetachObject(GLhandleARB containerObj, GLhandleARB attachedObj) {
    gl4es_glDetachShader(containerObj, attachedObj);
}

GLhandleARB APIENTRY_GL4ES gl4es_glCreateProgramObject(GLvoid) {
    return gl4es_glCreateProgram();
}

GLvoid APIENTRY_GL4ES gl4es_glAttachObject(GLhandleARB containerObj, GLhandleARB obj) {
    gl4es_glAttachShader(containerObj, obj);
}

GLvoid APIENTRY_GL4ES gl4es_glUseProgramObject(GLhandleARB programObj) {
    gl4es_glUseProgram(programObj);
}

GLvoid APIENTRY_GL4ES gl4es_glGetObjectParameterfv(GLhandleARB obj, GLenum pname, GLfloat* params) {
    if (!obj) return;
    // find type of obj (shader or program)
    program_t* glprogram = NULL;
    khint_t k_program;
    {
        khash_t(programlist)* programs = glstate->glsl->programs;
        k_program = kh_get(programlist, programs, obj);
        if (k_program != kh_end(programs)) glprogram = kh_value(programs, k_program);
    }
    // float, really?
    GLint p[4];
    if (glprogram)
        gl4es_glGetProgramiv(obj, pname, p);
    else
        gl4es_glGetShaderiv(obj, pname, p);

    params[0] = p[0]; // only 1-sized array here, right?
}
GLvoid APIENTRY_GL4ES gl4es_glGetObjectParameteriv(GLhandleARB obj, GLenum pname, GLint* params) {
    if (!obj) return;
    // find type of obj (shader or program)
    program_t* glprogram = NULL;
    khint_t k_program;
    {
        khash_t(programlist)* programs = glstate->glsl->programs;
        k_program = kh_get(programlist, programs, obj);
        if (k_program != kh_end(programs)) glprogram = kh_value(programs, k_program);
    }
    if (glprogram)
        gl4es_glGetProgramiv(obj, pname, params);
    else
        gl4es_glGetShaderiv(obj, pname, params);
    // hack, some programs don't take into account that the length returned by GL_OBJECT_INFO_LOG_LENGTH_ARB exclude the
    // '\0'.. so adding it here
    if (pname == GL_INFO_LOG_LENGTH) (*params)++;
}

GLvoid APIENTRY_GL4ES gl4es_glGetInfoLog(GLhandleARB obj, GLsizei maxLength, GLsizei* length, GLcharARB* infoLog) {
    if (!obj) return;
    // find type of obj (shader or program)
    program_t* glprogram = NULL;
    khint_t k_program;
    {
        khash_t(programlist)* programs = glstate->glsl->programs;
        k_program = kh_get(programlist, programs, obj);
        if (k_program != kh_end(programs)) glprogram = kh_value(programs, k_program);
    }

    if (glprogram)
        gl4es_glGetProgramInfoLog(obj, maxLength, length, infoLog);
    else
        gl4es_glGetShaderInfoLog(obj, maxLength, length, infoLog);
}

GLvoid APIENTRY_GL4ES gl4es_glGetAttachedObjects(GLhandleARB containerObj, GLsizei maxCount, GLsizei* count,
                                                 GLhandleARB* obj) {
    gl4es_glGetAttachedShaders(containerObj, maxCount, count, obj);
}

AliasExport(void, glAttachShader, , (GLuint program, GLuint shader));
AliasExport(void, glBindAttribLocation, , (GLuint program, GLuint index, const GLchar* name));
AliasExport_V(GLuint, glCreateProgram);
AliasExport(void, glDeleteProgram, , (GLuint program));
AliasExport(void, glDetachShader, , (GLuint program, GLuint shader));
AliasExport(void, glGetActiveAttrib, ,
            (GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size, GLenum* type, GLchar* name));
AliasExport(void, glGetAttachedShaders, , (GLuint program, GLsizei maxCount, GLsizei* count, GLuint* shaders));
AliasExport(GLint, glGetAttribLocation, , (GLuint program, const GLchar* name));
AliasExport(void, glGetActiveUniform, ,
            (GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size, GLenum* type, GLchar* name));
AliasExport(void, glGetProgramiv, , (GLuint program, GLenum pname, GLint* params));
AliasExport(GLint, glGetUniformLocation, , (GLuint program, const GLchar* name));
AliasExport(GLboolean, glIsProgram, , (GLuint program));
AliasExport(void, glLinkProgram, , (GLuint program));
AliasExport(void, glUseProgram, , (GLuint program));
AliasExport(void, glValidateProgram, , (GLuint program));
AliasExport(void, glGetProgramInfoLog, , (GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog));

AliasExport(void, glGetProgramBinary, ,
            (GLuint program, GLsizei bufSize, GLsizei* length, GLenum* binaryFormat, void* binary));
AliasExport(void, glProgramBinary, , (GLuint program, GLenum binaryFormat, const void* binary, GLsizei length));

// ================ GL_ARB_vertex_shader =================
AliasExport(GLvoid, glBindAttribLocation, ARB, (GLhandleARB programObj, GLuint index, const GLcharARB* name));

AliasExport(GLvoid, glGetActiveAttrib, ARB,
            (GLhandleARB programObj, GLuint index, GLsizei maxLength, GLsizei* length, GLint* size, GLenum* type,
             GLcharARB* name));

AliasExport(GLint, glGetAttribLocation, ARB, (GLhandleARB programObj, const GLcharARB* name));

AliasExport(GLvoid, glDeleteObject, ARB, (GLhandleARB obj));
AliasExport(GLhandleARB, glGetHandle, ARB, (GLenum pname));
AliasExport(GLvoid, glDetachObject, ARB, (GLhandleARB containerObj, GLhandleARB attachedObj));
AliasExport(GLhandleARB, glCreateProgramObject, ARB, ());
AliasExport(GLvoid, glAttachObject, ARB, (GLhandleARB containerObj, GLhandleARB obj));
AliasExport(GLvoid, glLinkProgram, ARB, (GLhandleARB programObj));
AliasExport(GLvoid, glUseProgramObject, ARB, (GLhandleARB programObj));
AliasExport(GLvoid, glValidateProgram, ARB, (GLhandleARB programObj));
AliasExport(GLvoid, glGetObjectParameterfv, ARB, (GLhandleARB obj, GLenum pname, GLfloat* params));
AliasExport(GLvoid, glGetObjectParameteriv, ARB, (GLhandleARB obj, GLenum pname, GLint* params));
AliasExport(GLvoid, glGetInfoLog, ARB, (GLhandleARB obj, GLsizei maxLength, GLsizei* length, GLcharARB* infoLog));
AliasExport(GLvoid, glGetAttachedObjects, ARB,
            (GLhandleARB containerObj, GLsizei maxCount, GLsizei* count, GLhandleARB* obj));
AliasExport(GLint, glGetUniformLocation, ARB, (GLhandleARB programObj, const GLcharARB* name));
AliasExport(GLvoid, glGetActiveUniform, ARB,
            (GLhandleARB programObj, GLuint index, GLsizei maxLength, GLsizei* length, GLint* size, GLenum* type,
             GLcharARB* name));
