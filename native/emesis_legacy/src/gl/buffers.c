#include <GLES/gl3.h>
#include "buffers.h"

#include "GLES3/gl32.h"
#include "khash.h"
#include "../glx/hardext.h"
#include "attributes.h"
#include "debug.h"
#include "gl4es.h"
#include "glstate.h"
#include "glstate.h"
#include "logs.h"
#include "init.h"
#include "loader.h"

// #define DEBUG
#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

KHASH_MAP_IMPL_INT(buff, glbuffer_t*);
KHASH_MAP_IMPL_INT(glvao, glvao_t*);

static GLuint lastbuffer = 1;

// Utility function to bind / unbind a particular buffer

static int emesis_has_gpu_copy_buffer(void) {
    static int checked = 0;
    static int available = 0;
    if (!checked && hardext.esversion >= 3) {
        checked = 1;
        if (gles && proc_address(gles, "glCopyBufferSubData") != NULL) {
            available = 1;
        }
    }
    return available;
}

void emesis_note_buffer_vao_ref(glbuffer_t* buff, glvao_t* vao) {
    if (!buff || !vao || !glstate || vao == glstate->defaultvao || vao->array == 0) return;
    if (buff->vao_ref_hint == 0) {
        buff->single_vao_id = vao->array;
        buff->vao_ref_hint = 1;
    } else if (buff->vao_ref_hint == 1 && buff->single_vao_id != vao->array) {
        buff->single_vao_id = 0;
        buff->vao_ref_hint = 2;
    }
}

static inline void emesis_scrub_vao_buffer(glvao_t* v, glbuffer_t* buff) {
    if (!v || !buff) return;
    if (v->vertex == buff) v->vertex = NULL;
    if (v->elements == buff) v->elements = NULL;
    if (v->pack == buff) v->pack = NULL;
    if (v->unpack == buff) v->unpack = NULL;
    if (v->read == buff) v->read = NULL;
    if (v->write == buff) v->write = NULL;
    if (v->uniform == buff) v->uniform = NULL;
    if (v->textureBuffer == buff) v->textureBuffer = NULL;
    if (v->indirect == buff) v->indirect = NULL;
    for (int j = 0; j < hardext.maxvattrib; j++) {
        if (v->vertexattrib[j].buffer == buff) {
            v->vertexattrib[j].buffer = NULL;
            v->vertexattrib[j].real_buffer = 0;
            v->vertexattrib[j].real_pointer = 0;
        }
    }
}

static inline void emesis_sync_global_vao_bindings(glvao_t* dst, const glvao_t* src) {
    if (!dst || !src || dst == src) return;
    dst->vertex = src->vertex;
    dst->pack = src->pack;
    dst->unpack = src->unpack;
    dst->read = src->read;
    dst->write = src->write;
    dst->uniform = src->uniform;
    dst->textureBuffer = src->textureBuffer;
    dst->indirect = src->indirect;
}

glbuffer_t** BUFF(GLenum target) {
    glvao_t* holder = (target == GL_ELEMENT_ARRAY_BUFFER || !glstate->defaultvao)
                          ? glstate->vao
                          : glstate->defaultvao;
    switch (target) {
    case GL_ARRAY_BUFFER:
        return &holder->vertex;
    case GL_ELEMENT_ARRAY_BUFFER:
        return &glstate->vao->elements;
    case GL_PIXEL_PACK_BUFFER:
        return &holder->pack;
    case GL_PIXEL_UNPACK_BUFFER:
        return &holder->unpack;
    case GL_COPY_READ_BUFFER:
        return &holder->read;
    case GL_COPY_WRITE_BUFFER:
        return &holder->write;
    case GL_UNIFORM_BUFFER:
        return &holder->uniform;
    case GL_TEXTURE_BUFFER:
        return &holder->textureBuffer;
    case GL_DRAW_INDIRECT_BUFFER:
        return &holder->indirect;

    default:
        LOGD("Warning, unknown buffer target 0x%04X\n", target);
    }
    return (glbuffer_t**)NULL;
}

void unbind_buffer(GLenum target) {
    glbuffer_t** t = BUFF(target);
    if (t) *t = (glbuffer_t*)NULL;
    if (target != GL_ELEMENT_ARRAY_BUFFER && glstate && glstate->defaultvao && glstate->vao != glstate->defaultvao) {
        emesis_sync_global_vao_bindings(glstate->vao, glstate->defaultvao);
    }
}
void bind_buffer(GLenum target, glbuffer_t* buff) {
    glbuffer_t** t = BUFF(target);
    if (t) *t = buff;
    if (target != GL_ELEMENT_ARRAY_BUFFER && glstate && glstate->defaultvao && glstate->vao != glstate->defaultvao) {
        emesis_sync_global_vao_bindings(glstate->vao, glstate->defaultvao);
    }
    if (buff) {
        if (target == GL_DRAW_INDIRECT_BUFFER) {
            buff->is_indirect = 1;
        } else if (target == GL_ELEMENT_ARRAY_BUFFER || target == GL_UNIFORM_BUFFER ||
                   target == GL_TEXTURE_BUFFER || target == GL_COPY_READ_BUFFER ||
                   target == GL_COPY_WRITE_BUFFER) {
            buff->used_by_divisor0_draw = 1;
            if (buff->gpu_dirty) {
                emesis_sync_gpu_buffer(buff, target);
            }
        }
    }
    if (target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER) {
        emesis_note_buffer_vao_ref(buff, glstate ? glstate->vao : NULL);
    }
}
glbuffer_t* getbuffer_buffer(GLenum target) {
    glbuffer_t** t = BUFF(target);
    if (t) return *t;
    return NULL;
}
glbuffer_t* getbuffer_id(GLuint buffer) {
    if (!buffer) return NULL;
    khint_t k;
    khash_t(buff)* list = glstate->buffers;
    k = kh_get(buff, list, buffer);
    if (k == kh_end(list)) return NULL;
    return kh_value(list, k);
}

int buffer_target(GLenum target) {
    switch (target) {
    case GL_ARRAY_BUFFER:
    case GL_ELEMENT_ARRAY_BUFFER:
    case GL_PIXEL_PACK_BUFFER:
    case GL_PIXEL_UNPACK_BUFFER:
    case GL_COPY_READ_BUFFER:
    case GL_COPY_WRITE_BUFFER:
    case GL_UNIFORM_BUFFER:
    case GL_TEXTURE_BUFFER:
    case GL_DRAW_INDIRECT_BUFFER:
        return 1;
    }
    return 0;
}

void rebind_real_buff_arrays(int old_buffer, int new_buffer) {
    for (int j = 0; j < hardext.maxvattrib; j++) {
        if (glstate->vao->vertexattrib[j].real_buffer == old_buffer) {
            glstate->vao->vertexattrib[j].real_buffer = new_buffer;
            /*if(!new_buffer)
                glstate->vao->vertexattrib[j].real_pointer = 0;*/
        }
    }
}

void emesis_sync_gpu_buffer(glbuffer_t* buff, GLenum hint_target) {
    if (!buff) return;
    buff->used_by_divisor0_draw = 1;
    if (!buff->gpu_dirty && buff->real_buffer) return;
    GLuint prev_real = buff->real_buffer;
    LOAD_GLES(glBufferData);
    LOAD_GLES(glGenBuffers);
    if (!buff->real_buffer) {
        gles_glGenBuffers(1, &buff->real_buffer);
        if (!buff->real_buffer) return;
    }
    GLenum up_tgt = (hint_target == GL_ELEMENT_ARRAY_BUFFER) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    GLuint prev_want = glstate->bind_buffer.want_index;
    GLuint prev_idx = glstate->bind_buffer.index;
    bindBuffer(up_tgt, buff->real_buffer);
    gles_glBufferData(up_tgt, buff->size, (buff->size > 0) ? buff->data : NULL,
                      buff->usage ? buff->usage : GL_STATIC_DRAW);
    if (up_tgt == GL_ELEMENT_ARRAY_BUFFER && prev_idx != buff->real_buffer) {
        bindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_idx);
        glstate->bind_buffer.want_index = prev_want;
    }
    buff->gpu_dirty = 0;
    if (buff->real_buffer != prev_real && glstate) {
        for (int i = 0; i < hardext.maxvattrib; ++i) {
            if (glstate->vao->vertexattrib[i].buffer == buff) {
                glstate->vao->vertexattrib[i].real_buffer = buff->real_buffer;
            }
        }
    }
}

void VISIBLE glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) {
    DBG(SHUT_LOGD("glTexBuffer(%s, %s, %u)\n", PrintEnum(target), PrintEnum(internalformat), buffer);)
    LOAD_GLES3(glTexBuffer);
    glbuffer_t* realBuffer = getbuffer_id(buffer);
    if (realBuffer) emesis_sync_gpu_buffer(realBuffer, GL_ARRAY_BUFFER);
    gles_glTexBuffer(target, internalformat, realBuffer ? realBuffer->real_buffer : 0);
}

void APIENTRY_GL4ES gl4es_glGenBuffers(GLsizei n, GLuint* buffers) {
    DBG(SHUT_LOGD("glGenBuffers(%i, %p)\n", n, buffers);)
    noerrorShim();
    if (n < 1) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    khash_t(buff)* list = glstate->buffers;
    for (int i = 0; i < n; i++) { // create buffer, and check uniqueness...
        int b;
        while (getbuffer_id(b = lastbuffer++))
            ;
        buffers[i] = b;
        // create the buffer
        khint_t k;
        int ret;
        k = kh_put(buff, list, b, &ret);
        glbuffer_t* buff = kh_value(list, k) = calloc(1, sizeof(glbuffer_t));
        buff->buffer = b;
        buff->type = 0; // no target for now
        buff->data = NULL;
        buff->usage = GL_STATIC_DRAW;
        buff->size = 0;
        buff->cap = 0;
        buff->access = GL_READ_WRITE;
        buff->mapped = 0;
        buff->real_buffer = 0;
    }
}

void APIENTRY_GL4ES gl4es_glBindBuffer(GLenum target, GLuint buffer) {
    DBG(SHUT_LOGD("glBindBuffer(%s, %u)\n", PrintEnum(target), buffer);)
    // this flush is probably not needed as long as real VBO are not used
    FLUSH_BEGINEND;

    khint_t k;
    int ret;
    khash_t(buff)* list = glstate->buffers;
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return;
    }
    // if buffer = 0 => unbind buffer!
    if (buffer == 0) {
        // unbind buffer
        bindBuffer(target, 0);
        unbind_buffer(target);
    } else {
        // search for an existing buffer
        k = kh_get(buff, list, buffer);
        glbuffer_t* buff = NULL;
        if (k == kh_end(list)) {
            k = kh_put(buff, list, buffer, &ret);
            buff = kh_value(list, k) = calloc(1, sizeof(glbuffer_t));
            buff->buffer = buffer;
            buff->type = target;
            buff->data = NULL;
            buff->usage = GL_STATIC_DRAW;
            buff->size = 0;
            buff->cap = 0;
            buff->access = GL_READ_WRITE;
            buff->mapped = 0;
            buff->real_buffer = 0;
        } else {
            buff = kh_value(list, k);
            buff->type = target; // TODO: check if old binding?
        }
        bind_buffer(target, buff);
    }
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glBufferData(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage) {
    DBG(SHUT_LOGD("glBufferData(%s, %zi, %p, %s)\n", PrintEnum(target), size, data, PrintEnum(usage));)
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return;
    }
    glbuffer_t* buff = getbuffer_buffer(target);
    if (buff == NULL) {
        errorShim(GL_INVALID_OPERATION);
        LOGE("Warning, null buffer for target=0x%04X for glBufferData\n", target);
        return;
    }
    if (target == GL_ARRAY_BUFFER) VaoSharedClear(glstate->vao);

    int is_cpu_emul_target = (target == GL_PIXEL_PACK_BUFFER || target == GL_PIXEL_UNPACK_BUFFER ||
                              target == GL_DRAW_INDIRECT_BUFFER);
    if (target == GL_DRAW_INDIRECT_BUFFER) {
        buff->is_indirect = 1;
    }

    int go_real = 0;
    if ((target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER) && globals4es.usevbo)
        go_real = 1;

    if (target == GL_UNIFORM_BUFFER || target == GL_COPY_WRITE_BUFFER || target == GL_COPY_READ_BUFFER ||
        target == GL_TEXTURE_BUFFER)
        go_real = 1;

    /* Fast path for CPU-consumed per-pass buffers (e.g. Sodium MultidrawChunkRenderBackend
       commandBuffer on GL_DRAW_INDIRECT_BUFFER and uniformBuffer on divisor>0 ModelOffset):
       avoid 8x/frame Mali kbase glBufferData kernel page allocations unless GPU actually binds it */
    int defer_gpu_upload = 0;
    if (is_cpu_emul_target && !buff->used_by_divisor0_draw) {
        defer_gpu_upload = 1;
        go_real = 0;
    } else if (target == GL_ARRAY_BUFFER && buff->real_buffer &&
               (buff->is_indirect || buff->used_by_divisor) && !buff->used_by_divisor0_draw) {
        defer_gpu_upload = 1;
    }

    GLuint prev_real_buffer = buff->real_buffer;
    if (buff->real_buffer && !go_real && !defer_gpu_upload) {
        rebind_real_buff_arrays(buff->real_buffer, 0);

        deleteSingleBuffer(buff->real_buffer);
        // what about VA already pointing there?
        buff->real_buffer = 0;
    }
    if (go_real && !defer_gpu_upload) {
        if (!buff->real_buffer) {
            LOAD_GLES(glGenBuffers);
            gles_glGenBuffers(1, &buff->real_buffer);
        }
        GLenum hw_target = is_cpu_emul_target ? GL_ARRAY_BUFFER : target;
        LOAD_GLES(glBufferData);
        LOAD_GLES(glBindBuffer);
        bindBuffer(hw_target, buff->real_buffer);
        gles_glBufferData(hw_target, size, data, usage);
        buff->gpu_dirty = 0;
        DBG(SHUT_LOGD(" => real VBO %d\n", buff->real_buffer);)
    } else if (defer_gpu_upload) {
        buff->gpu_dirty = 1;
    }

    int skip_cpu_shadow = (!defer_gpu_upload && !is_cpu_emul_target &&
                           target != GL_ELEMENT_ARRAY_BUFFER &&
                           go_real && buff->real_buffer && emesis_has_gpu_copy_buffer() &&
                           !buff->mapped && !buff->used_by_divisor);
    if (skip_cpu_shadow) {
        if (buff->data) {
            free(buff->data);
            buff->data = NULL;
        }
        buff->cap = 0;
    } else {
        if (size > buff->cap || !buff->data) {
            GLsizeiptr new_cap = (size > 0) ? size : 16;
            if (buff->cap > 0 && new_cap < buff->cap * 2 && new_cap <= (4 * 1024 * 1024)) {
                new_cap = buff->cap * 2;
            }
            if (buff->data) free(buff->data);
            buff->data = malloc((size_t)new_cap);
            buff->cap = buff->data ? new_cap : 0;
        }
        if (data && buff->data && size > 0) memcpy(buff->data, data, (size_t)size);
    }
    buff->size = size;
    buff->usage = usage;
    DBG(SHUT_LOGD("\t buff->data = %p (size=%zd, cap=%zd)\n", buff->data, size, buff->cap);)
    buff->access = GL_READ_WRITE;
    // update binded VA only when real_buffer handle actually changed
    if (buff->real_buffer != prev_real_buffer) {
        for (int i = 0; i < hardext.maxvattrib; ++i) {
            vertexattrib_t* v = &glstate->vao->vertexattrib[i];
            if (v->buffer == buff) {
                v->real_buffer = v->buffer->real_buffer;
            }
        }
        if (glstate->defaultvao && glstate->defaultvao != glstate->vao) {
            for (int i = 0; i < hardext.maxvattrib; ++i) {
                if (glstate->defaultvao->vertexattrib[i].buffer == buff) {
                    glstate->defaultvao->vertexattrib[i].real_buffer = buff->real_buffer;
                }
            }
        }
        if (buff->vao_ref_hint == 1 && buff->single_vao_id != 0 && glstate->vaos) {
            khint_t vk = kh_get(glvao, glstate->vaos, buff->single_vao_id);
            if (vk != kh_end(glstate->vaos)) {
                glvao_t* sv = kh_value(glstate->vaos, vk);
                if (sv && sv != glstate->vao) {
                    for (int i = 0; i < hardext.maxvattrib; ++i) {
                        if (sv->vertexattrib[i].buffer == buff) {
                            sv->vertexattrib[i].real_buffer = buff->real_buffer;
                        }
                    }
                }
            }
        } else if (buff->vao_ref_hint >= 2 && glstate->vaos) {
            glvao_t* sv;
            kh_foreach_value(glstate->vaos, sv, {
                for (int i = 0; i < hardext.maxvattrib; ++i) {
                    if (sv->vertexattrib[i].buffer == buff) {
                        sv->vertexattrib[i].real_buffer = buff->real_buffer;
                    }
                }
            });
        }
        if (glstate->clientStack) {
            for (int s = 0; s < glstate->clientStack->len; ++s) {
                if (glstate->clientStack[s].mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
                    for (int i = 0; i < NB_VA; ++i) {
                        if (glstate->clientStack[s].vertexattrib[i].buffer == buff) {
                            glstate->clientStack[s].vertexattrib[i].real_buffer = buff->real_buffer;
                        }
                    }
                }
            }
        }
    }
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glNamedBufferData(GLuint buffer, GLsizeiptr size, const GLvoid* data, GLenum usage) {
    DBG(SHUT_LOGD("glNamedBufferData(%u, %zi, %p, %s)\n", buffer, size, data, PrintEnum(usage));)
    glbuffer_t* buff = getbuffer_id(buffer);
    if (buff == NULL) {
        DBG(SHUT_LOGD("Named Buffer not found\n");)
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    int go_real = 0;
    if ((buff->type == GL_ARRAY_BUFFER || buff->type == GL_ELEMENT_ARRAY_BUFFER) && globals4es.usevbo)
        go_real = 1;

    if (buff->type == GL_UNIFORM_BUFFER || buff->type == GL_COPY_WRITE_BUFFER || buff->type == GL_COPY_READ_BUFFER ||
        buff->type == GL_TEXTURE_BUFFER || buff->type == GL_PIXEL_PACK_BUFFER || buff->type == GL_PIXEL_UNPACK_BUFFER ||
        buff->type == GL_DRAW_INDIRECT_BUFFER)
        go_real = 1;

    GLuint prev_real_buffer = buff->real_buffer;
    if (buff->real_buffer && !go_real) {
        deleteSingleBuffer(buff->real_buffer);
        // what about VA already pointing there?
        buff->real_buffer = 0;
    }
    if (go_real) {
        if (!buff->real_buffer) {
            LOAD_GLES(glGenBuffers);
            gles_glGenBuffers(1, &buff->real_buffer);
        }
        LOAD_GLES(glBufferData);
        LOAD_GLES(glBindBuffer);
        GLenum upload_target = (buff->type == GL_ELEMENT_ARRAY_BUFFER)
                                   ? GL_ELEMENT_ARRAY_BUFFER
                                   : ((hardext.esversion >= 3 && buff->type &&
                                       buff->type != GL_PIXEL_PACK_BUFFER &&
                                       buff->type != GL_PIXEL_UNPACK_BUFFER &&
                                       buff->type != GL_DRAW_INDIRECT_BUFFER)
                                          ? buff->type
                                          : GL_ARRAY_BUFFER);
        bindBuffer(upload_target, buff->real_buffer);
        gles_glBufferData(upload_target, size, data, usage);
    }

    int skip_cpu_shadow = 0;
    if (go_real && buff->real_buffer && emesis_has_gpu_copy_buffer() &&
        buff->type != GL_ELEMENT_ARRAY_BUFFER) {
        if (buff->type == GL_COPY_WRITE_BUFFER || buff->type == GL_COPY_READ_BUFFER ||
            buff->type == GL_PIXEL_PACK_BUFFER || buff->type == GL_PIXEL_UNPACK_BUFFER ||
            usage == GL_STREAM_COPY || usage == GL_DYNAMIC_COPY || usage == GL_STATIC_COPY ||
            (data == NULL && size >= 65536)) {
            skip_cpu_shadow = 1;
        }
    }

    if (skip_cpu_shadow) {
        if (buff->data) {
            free(buff->data);
            buff->data = NULL;
        }
        buff->cap = 0;
    } else {
        if (size > buff->cap || !buff->data) {
            GLsizeiptr new_cap = (size > 0) ? size : 16;
            if (buff->cap > 0 && new_cap < buff->cap * 2 && new_cap <= (4 * 1024 * 1024)) {
                new_cap = buff->cap * 2;
            }
            if (buff->data) free(buff->data);
            buff->data = malloc((size_t)new_cap);
            buff->cap = buff->data ? new_cap : 0;
        }
        if (data && buff->data && size > 0) memcpy(buff->data, data, (size_t)size);
    }
    buff->size = size;
    buff->usage = usage;
    buff->access = GL_READ_WRITE;
    // update binded VA only if real_buffer changed
    if (buff->real_buffer != prev_real_buffer) {
        if (glstate->defaultvao) {
            for (int i = 0; i < hardext.maxvattrib; ++i) {
                vertexattrib_t* v = &glstate->defaultvao->vertexattrib[i];
                if (v->buffer == buff) v->real_buffer = buff->real_buffer;
            }
        }
        if (buff->vao_ref_hint == 1 && buff->single_vao_id != 0 && glstate->vaos) {
            khint_t vk = kh_get(glvao, glstate->vaos, buff->single_vao_id);
            if (vk != kh_end(glstate->vaos)) {
                glvao_t* svao = kh_value(glstate->vaos, vk);
                if (svao) {
                    for (int i = 0; i < hardext.maxvattrib; ++i) {
                        vertexattrib_t* v = &svao->vertexattrib[i];
                        if (v->buffer == buff) v->real_buffer = buff->real_buffer;
                    }
                }
            }
        } else if (buff->vao_ref_hint >= 2 && glstate->vaos) {
            glvao_t* vao;
            kh_foreach_value(glstate->vaos, vao, {
                for (int i = 0; i < hardext.maxvattrib; ++i) {
                    vertexattrib_t* v = &vao->vertexattrib[i];
                    if (v->buffer == buff) v->real_buffer = buff->real_buffer;
                }
            });
        }
    }
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data) {
    DBG(SHUT_LOGD("glBufferSubData(%s, %p, %zi, %p)\n", PrintEnum(target), (void*)offset, size, data);)
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return;
    }
    glbuffer_t* buff = getbuffer_buffer(target);
    if (buff == NULL) {
        errorShim(GL_INVALID_OPERATION);
        DBG(SHUT_LOGD("LIBGL: Warning, null buffer for target=0x%04X for glBufferSubData\n", target);)
        return;
    }

    if (target == GL_ARRAY_BUFFER) VaoSharedClear(glstate->vao);

    if (offset < 0 || size < 0 || offset + size > buff->size) {
        errorShim(GL_INVALID_VALUE);
        return;
    }

    if ((target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER || target == GL_UNIFORM_BUFFER ||
         target == GL_COPY_WRITE_BUFFER || target == GL_COPY_READ_BUFFER || target == GL_TEXTURE_BUFFER) &&
        buff->real_buffer && !buff->gpu_dirty) {
        LOAD_GLES(glBufferSubData);
        LOAD_GLES(glBindBuffer);
        bindBuffer(target, buff->real_buffer);
        gles_glBufferSubData(target, offset, size, data);
    } else if (buff->real_buffer) {
        buff->gpu_dirty = 1;
    }

    if (!buff->data && !buff->real_buffer && buff->size > 0) {
        buff->data = calloc(1, (size_t)buff->size);
        buff->cap = buff->data ? buff->size : 0;
    }
    if (buff->data && data && size > 0) memcpy((char*)buff->data + offset, data, (size_t)size);
    noerrorShim();
}
void APIENTRY_GL4ES gl4es_glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid* data) {
    DBG(SHUT_LOGD("glNamedBufferSubData(%u, %p, %zi, %p)\n", buffer, (void*)offset, size, data);)
    glbuffer_t* buff = getbuffer_id(buffer);
    if (buff == NULL) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }

    if (offset < 0 || size < 0 || offset + size > buff->size) {
        errorShim(GL_INVALID_VALUE);
        return;
    }

    if ((buff->type == GL_ARRAY_BUFFER || buff->type == GL_ELEMENT_ARRAY_BUFFER || buff->type == GL_UNIFORM_BUFFER ||
         buff->type == GL_COPY_WRITE_BUFFER || buff->type == GL_COPY_READ_BUFFER || buff->type == GL_TEXTURE_BUFFER ||
         buff->type == GL_PIXEL_PACK_BUFFER || buff->type == GL_PIXEL_UNPACK_BUFFER ||
         buff->type == GL_DRAW_INDIRECT_BUFFER) &&
        buff->real_buffer) {
        LOAD_GLES(glBufferSubData);
        LOAD_GLES(glBindBuffer);
        GLenum sub_target = (buff->type == GL_PIXEL_PACK_BUFFER || buff->type == GL_PIXEL_UNPACK_BUFFER ||
                             buff->type == GL_DRAW_INDIRECT_BUFFER)
                                ? ((hardext.esversion >= 3) ? GL_COPY_READ_BUFFER : GL_ARRAY_BUFFER)
                                : buff->type;
        bindBuffer(sub_target, buff->real_buffer);
        gles_glBufferSubData(sub_target, offset, size, data);
    }
    if (!buff->data && !buff->real_buffer && buff->size > 0) {
        buff->data = calloc(1, (size_t)buff->size);
        buff->cap = buff->data ? buff->size : 0;
    }
    if (buff->data && data && size > 0) memcpy((char*)buff->data + offset, data, (size_t)size);
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    DBG(SHUT_LOGD("glDeleteBuffers(%i, %p)\n", n, buffers);)
    if (!glstate) return;
    FLUSH_BEGINEND;

    VaoSharedClear(glstate->vao);
    khash_t(buff)* list = glstate->buffers;
    if (list) {
        khint_t k;
        glbuffer_t* buff;
        for (int i = 0; i < n; i++) {
            GLuint t = buffers[i];
            DBG(SHUT_LOGD("\t deleting %d\n", t);)
            if (t) { // don't allow to remove default one
                k = kh_get(buff, list, t);
                if (k != kh_end(list)) {
                    buff = kh_value(list, k);
                    if (buff->real_buffer) {
                        rebind_real_buff_arrays(buff->real_buffer, 0); // unbind
                        LOAD_GLES(glDeleteBuffers);
                        deleteSingleBuffer(buff->real_buffer);
                    }
                    if (glstate->gleshard) {
                        LOAD_GLES2(glDisableVertexAttribArray);
                        for (int j = 0; j < hardext.maxvattrib; j++) {
                            if (glstate->gleshard->vertexattrib[j].buffer == buff) {
                                if (glstate->gleshard->vertexattrib[j].enabled && hardext.esversion > 1 &&
                                    gles_glDisableVertexAttribArray) {
                                    gles_glDisableVertexAttribArray(j);
                                    glstate->gleshard->vertexattrib[j].enabled = 0;
                                    if (j < 32) glstate->gleshard->enabled_va_mask &= ~(1u << (unsigned)j);
                                }
                                glstate->gleshard->vertexattrib[j].buffer = NULL;
                                glstate->gleshard->vertexattrib[j].real_buffer = 0;
                                glstate->gleshard->vertexattrib[j].real_pointer = 0;
                            }
                        }
                    }
                    emesis_scrub_vao_buffer(glstate->defaultvao, buff);
                    if (glstate->vao && glstate->vao != glstate->defaultvao) {
                        emesis_scrub_vao_buffer(glstate->vao, buff);
                    }
                    if (buff->vao_ref_hint == 1 && buff->single_vao_id != 0 && glstate->vaos) {
                        khint_t vk = kh_get(glvao, glstate->vaos, buff->single_vao_id);
                        if (vk != kh_end(glstate->vaos)) {
                            glvao_t* sv = kh_value(glstate->vaos, vk);
                            if (sv && sv != glstate->vao) {
                                emesis_scrub_vao_buffer(sv, buff);
                            }
                        }
                    } else if (buff->vao_ref_hint >= 2 && glstate->vaos) {
                        glvao_t* v;
                        kh_foreach_value(glstate->vaos, v, {
                            if (v && v != glstate->vao) {
                                emesis_scrub_vao_buffer(v, buff);
                            }
                        });
                    }
                    if (glstate->clientStack) {
                        for (int s = 0; s < glstate->clientStack->len; ++s) {
                            if (glstate->clientStack[s].mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
                                for (int j = 0; j < NB_VA; ++j) {
                                    if (glstate->clientStack[s].vertexattrib[j].buffer == buff) {
                                        glstate->clientStack[s].vertexattrib[j].buffer = NULL;
                                        glstate->clientStack[s].vertexattrib[j].real_buffer = 0;
                                        glstate->clientStack[s].vertexattrib[j].real_pointer = 0;
                                        glstate->clientStack[s].vertexattrib[j].pointer = NULL;
                                        glstate->clientStack[s].vertexattrib[j].enabled = 0;
                                    }
                                }
                            }
                        }
                    }
                    DBG(SHUT_LOGD("\t buff->data = %p\n", buff->data);)
                    if (buff->data) free(buff->data);
                    kh_del(buff, list, k);
                    free(buff);
                }
            }
        }
    }
    DBG(SHUT_LOGD("\t done\n");)
    noerrorShim();
}

GLboolean APIENTRY_GL4ES gl4es_glIsBuffer(GLuint buffer) {
    DBG(SHUT_LOGD("glIsBuffer(%u)\n", buffer);)
    khash_t(buff)* list = glstate->buffers;
    khint_t k;
    noerrorShim();
    if (list) {
        k = kh_get(buff, list, buffer);
        if (k != kh_end(list)) {
            return GL_TRUE;
        }
    }
    return GL_FALSE;
}

static void bufferGetParameteriv(glbuffer_t* buff, GLenum value, GLint* data) {
    noerrorShim();
    switch (value) {
    case GL_BUFFER_ACCESS:
        data[0] = buff->access;
        break;
    case GL_BUFFER_ACCESS_FLAGS:
        data[0] = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
        break;
    case GL_BUFFER_MAPPED:
        data[0] = (buff->mapped) ? GL_TRUE : GL_FALSE;
        break;
    case GL_BUFFER_MAP_LENGTH:
        data[0] = (buff->mapped) ? buff->size : 0;
        break;
    case GL_BUFFER_MAP_OFFSET:
        data[0] = 0;
        break;
    case GL_BUFFER_SIZE:
        data[0] = buff->size;
        break;
    case GL_BUFFER_USAGE:
        data[0] = buff->usage;
        break;
    default:
        DBG(SHUT_LOGD("[ERROR] bufferGetParameteriv Unexpected: %s", PrintEnum(value));)
        errorShim(GL_INVALID_ENUM);
        /* TODO Error if something else */
    }
}

void APIENTRY_GL4ES gl4es_glGetBufferParameteriv(GLenum target, GLenum value, GLint* data) {
    DBG(SHUT_LOGD("glGetBufferParameteriv(%s, %s, %p)\n", PrintEnum(target), PrintEnum(value), data);)
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return;
    }
    glbuffer_t* buff = getbuffer_buffer(target);
    if (buff == NULL) {
        errorShim(GL_INVALID_OPERATION);
        return; // Should generate an error!
    }
    bufferGetParameteriv(buff, value, data);
}
void APIENTRY_GL4ES gl4es_glGetNamedBufferParameteriv(GLuint buffer, GLenum value, GLint* data) {
    DBG(SHUT_LOGD("glGetNamedBufferParameteriv(%u, %s, %p)\n", buffer, PrintEnum(value), data);)
    glbuffer_t* buff = getbuffer_id(buffer);
    if (buff == NULL) {
        errorShim(GL_INVALID_OPERATION);
        return; // Should generate an error!
    }
    bufferGetParameteriv(buff, value, data);
}

void emesis_ensure_buffer_cpu_shadow(glbuffer_t* buff, GLenum target, int need_read) {
    if (!buff || buff->data) return;
    size_t alloc_sz = (buff->size > 0) ? (size_t)buff->size : 16u;
    buff->data = calloc(1, alloc_sz);
    buff->cap = buff->data ? (GLsizeiptr)alloc_sz : 0;
    if (buff->data && need_read && buff->real_buffer && buff->size > 0 && hardext.esversion >= 3) {
        GLenum map_target = target ? target : (buff->type ? buff->type : GL_COPY_READ_BUFFER);
        if (map_target == GL_PIXEL_PACK_BUFFER || map_target == GL_PIXEL_UNPACK_BUFFER ||
            map_target == GL_DRAW_INDIRECT_BUFFER) {
            map_target = GL_COPY_READ_BUFFER;
        }
        LOAD_GLES3(glMapBufferRange);
        LOAD_GLES3(glUnmapBuffer);
        if (gles_glMapBufferRange && gles_glUnmapBuffer) {
            bindBuffer(map_target, buff->real_buffer);
            void* gpu_ptr = gles_glMapBufferRange(map_target, 0, buff->size, GL_MAP_READ_BIT);
            if (gpu_ptr) {
                memcpy(buff->data, gpu_ptr, (size_t)buff->size);
                gles_glUnmapBuffer(map_target);
            }
        }
    }
}

void* APIENTRY_GL4ES gl4es_glMapBuffer(GLenum target, GLenum access) {
    DBG(SHUT_LOGD("glMapBuffer(%s, %s)\n", PrintEnum(target), PrintEnum(access));)
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return (void*)NULL;
    }

    if (target == GL_ARRAY_BUFFER) VaoSharedClear(glstate->vao);

    glbuffer_t* buff = getbuffer_buffer(target);
    if (buff == NULL) {
        errorShim(GL_INVALID_VALUE);
        return NULL;
    }
    if (buff->mapped) {
        errorShim(GL_INVALID_OPERATION);
        return NULL;
    }
    emesis_ensure_buffer_cpu_shadow(buff, target, (access == GL_READ_ONLY || access == GL_READ_WRITE));
    buff->access = access; // not used
    buff->mapped = 1;
    buff->ranged = 0;
    noerrorShim();
    return buff->data; // Not nice, should do some copy or something probably
}
void* APIENTRY_GL4ES gl4es_glMapNamedBuffer(GLuint buffer, GLenum access) {
    DBG(SHUT_LOGD("glMapNamedBuffer(%u, %s)\n", buffer, PrintEnum(access));)

    glbuffer_t* buff = getbuffer_id(buffer);
    if (buff == NULL) {
        errorShim(GL_INVALID_VALUE);
        return NULL;
    }
    if (buff->mapped) {
        errorShim(GL_INVALID_OPERATION);
        return NULL;
    }
    emesis_ensure_buffer_cpu_shadow(buff, buff->type, (access == GL_READ_ONLY || access == GL_READ_WRITE));
    buff->access = access; // not used
    buff->mapped = 1;
    buff->ranged = 0;
    noerrorShim();
    return buff->data; // Not nice, should do some copy or something probably
}

GLboolean APIENTRY_GL4ES gl4es_glUnmapBuffer(GLenum target) {
    DBG(SHUT_LOGD("glUnmapBuffer(%s)\n", PrintEnum(target));)
    if (glstate->list.compiling) {
        errorShim(GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    FLUSH_BEGINEND;

    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return GL_FALSE;
    }

    if (target == GL_ARRAY_BUFFER) VaoSharedClear(glstate->vao);

    glbuffer_t* buff = getbuffer_buffer(target);
    if (buff == NULL) {
        errorShim(GL_INVALID_VALUE);
        return GL_FALSE;
    }
    noerrorShim();

    GLenum upload_target = target ? target : buff->type;
    GLenum gles_upload_target = (upload_target == GL_PIXEL_PACK_BUFFER || upload_target == GL_PIXEL_UNPACK_BUFFER ||
                                 upload_target == GL_DRAW_INDIRECT_BUFFER || upload_target == 0)
                                    ? ((hardext.esversion >= 3) ? GL_COPY_READ_BUFFER : GL_ARRAY_BUFFER)
                                    : upload_target;
    if (buff->real_buffer && buff->data &&
        (upload_target == GL_ARRAY_BUFFER || upload_target == GL_ELEMENT_ARRAY_BUFFER ||
         upload_target == GL_UNIFORM_BUFFER || upload_target == GL_COPY_WRITE_BUFFER ||
         upload_target == GL_COPY_READ_BUFFER || upload_target == GL_TEXTURE_BUFFER ||
         upload_target == GL_PIXEL_PACK_BUFFER || upload_target == GL_PIXEL_UNPACK_BUFFER ||
         upload_target == GL_DRAW_INDIRECT_BUFFER) &&
        buff->mapped && !buff->ranged && (buff->access == GL_WRITE_ONLY || buff->access == GL_READ_WRITE)) {
        LOAD_GLES(glBufferSubData);
        LOAD_GLES(glBindBuffer);
        bindBuffer(gles_upload_target, buff->real_buffer);
        gles_glBufferSubData(gles_upload_target, 0, buff->size, buff->data);
    }

    if (buff->real_buffer && buff->data &&
        (upload_target == GL_ARRAY_BUFFER || upload_target == GL_ELEMENT_ARRAY_BUFFER ||
         upload_target == GL_UNIFORM_BUFFER || upload_target == GL_COPY_WRITE_BUFFER ||
         upload_target == GL_COPY_READ_BUFFER || upload_target == GL_TEXTURE_BUFFER ||
         upload_target == GL_PIXEL_PACK_BUFFER || upload_target == GL_PIXEL_UNPACK_BUFFER ||
         upload_target == GL_DRAW_INDIRECT_BUFFER) &&
        buff->mapped && buff->ranged && (buff->access & GL_MAP_WRITE_BIT_EXT) &&
        !(buff->access & GL_MAP_FLUSH_EXPLICIT_BIT_EXT)) {
        LOAD_GLES(glBufferSubData);
        bindBuffer(gles_upload_target, buff->real_buffer);
        gles_glBufferSubData(gles_upload_target, buff->offset, buff->length,
                             (void*)((uintptr_t)buff->data + buff->offset));
    }
    if (buff->mapped) {
        buff->mapped = 0;
        buff->ranged = 0;
        return GL_TRUE;
    }
    return GL_FALSE;
}
GLboolean APIENTRY_GL4ES gl4es_glUnmapNamedBuffer(GLuint buffer) {
    DBG(SHUT_LOGD("glUnmapNamedBuffer(%u)\n", buffer);)
    if (glstate->list.compiling) {
        errorShim(GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    FLUSH_BEGINEND;

    glbuffer_t* buff = getbuffer_id(buffer);
    if (buff == NULL) return GL_FALSE; // Should generate an error!
    noerrorShim();

    if (buff->real_buffer && buff->data &&
        (buff->type == GL_ARRAY_BUFFER || buff->type == GL_ELEMENT_ARRAY_BUFFER || buff->type == GL_UNIFORM_BUFFER ||
         buff->type == GL_COPY_WRITE_BUFFER || buff->type == GL_COPY_READ_BUFFER || buff->type == GL_TEXTURE_BUFFER ||
         buff->type == GL_PIXEL_PACK_BUFFER || buff->type == GL_PIXEL_UNPACK_BUFFER ||
         buff->type == GL_DRAW_INDIRECT_BUFFER) &&
        buff->mapped && !buff->ranged && (buff->access == GL_WRITE_ONLY || buff->access == GL_READ_WRITE)) {
        LOAD_GLES(glBufferSubData);
        LOAD_GLES(glBindBuffer);
        GLenum upload_target = (buff->type == GL_ELEMENT_ARRAY_BUFFER)
                                   ? GL_ELEMENT_ARRAY_BUFFER
                                   : ((hardext.esversion >= 3 && buff->type &&
                                       buff->type != GL_PIXEL_PACK_BUFFER &&
                                       buff->type != GL_PIXEL_UNPACK_BUFFER &&
                                       buff->type != GL_DRAW_INDIRECT_BUFFER)
                                          ? buff->type
                                          : GL_ARRAY_BUFFER);
        bindBuffer(upload_target, buff->real_buffer);
        gles_glBufferSubData(upload_target, 0, buff->size, buff->data);
    }

    if (buff->real_buffer && buff->data &&
        (buff->type == GL_ARRAY_BUFFER || buff->type == GL_ELEMENT_ARRAY_BUFFER || buff->type == GL_UNIFORM_BUFFER ||
         buff->type == GL_COPY_WRITE_BUFFER || buff->type == GL_COPY_READ_BUFFER || buff->type == GL_TEXTURE_BUFFER ||
         buff->type == GL_PIXEL_PACK_BUFFER || buff->type == GL_PIXEL_UNPACK_BUFFER ||
         buff->type == GL_DRAW_INDIRECT_BUFFER) &&
        buff->mapped && buff->ranged && (buff->access & GL_MAP_WRITE_BIT_EXT) &&
        !(buff->access & GL_MAP_FLUSH_EXPLICIT_BIT_EXT)) {
        LOAD_GLES(glBufferSubData);
        LOAD_GLES(glBindBuffer);
        GLenum upload_target = (buff->type == GL_ELEMENT_ARRAY_BUFFER)
                                   ? GL_ELEMENT_ARRAY_BUFFER
                                   : ((hardext.esversion >= 3 && buff->type &&
                                       buff->type != GL_PIXEL_PACK_BUFFER &&
                                       buff->type != GL_PIXEL_UNPACK_BUFFER &&
                                       buff->type != GL_DRAW_INDIRECT_BUFFER)
                                          ? buff->type
                                          : GL_ARRAY_BUFFER);
        bindBuffer(upload_target, buff->real_buffer);
        gles_glBufferSubData(upload_target, buff->offset, buff->length, (void*)((uintptr_t)buff->data + buff->offset));
    }
    if (buff->mapped) {
        buff->mapped = 0;
        buff->ranged = 0;
        return GL_TRUE;
    }
    return GL_FALSE;
}

void APIENTRY_GL4ES gl4es_glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, GLvoid* data) {
    DBG(SHUT_LOGD("glGetBufferSubData(%s, %p, %zi, %p)\n", PrintEnum(target), (void*)offset, size, data);)
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return;
    }
    glbuffer_t* buff = getbuffer_buffer(target);

    if (buff == NULL)
        return; // Should generate an error!
                // TODO, check parameter consistancie
    emesis_ensure_buffer_cpu_shadow(buff, target, 1);
    if (buff->data && data && size > 0) memcpy(data, (char*)buff->data + offset, (size_t)size);
    noerrorShim();
}
void APIENTRY_GL4ES gl4es_glGetNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, GLvoid* data) {
    DBG(SHUT_LOGD("glGetNamedBufferSubData(%u, %p, %zi, %p)\n", buffer, (void*)offset, size, data);)
    glbuffer_t* buff = getbuffer_id(buffer);

    if (buff == NULL)
        return; // Should generate an error!
                // TODO, check parameter consistancie
    emesis_ensure_buffer_cpu_shadow(buff, buff->type, 1);
    if (buff->data && data && size > 0) memcpy(data, (char*)buff->data + offset, (size_t)size);
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glGetBufferPointerv(GLenum target, GLenum pname, GLvoid** params) {
    DBG(SHUT_LOGD("glGetBufferPointerv(%s, %s, %p)\n", PrintEnum(target), PrintEnum(pname), params);)
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return;
    }
    glbuffer_t* buff = getbuffer_buffer(target);
    if (buff == NULL) return; // Should generate an error!
    if (pname != GL_BUFFER_MAP_POINTER) {
        errorShim(GL_INVALID_ENUM);
        return;
    }
    if (!buff->mapped) {
        params[0] = NULL;
    } else {
        params[0] = buff->data;
    }
}
void APIENTRY_GL4ES gl4es_glGetNamedBufferPointerv(GLuint buffer, GLenum pname, GLvoid** params) {
    DBG(SHUT_LOGD("glGetNamedBufferPointerv(%u, %s, %p)\n", buffer, PrintEnum(pname), params);)
    glbuffer_t* buff = getbuffer_id(buffer);
    if (buff == NULL) return; // Should generate an error!
    if (pname != GL_BUFFER_MAP_POINTER) {
        errorShim(GL_INVALID_ENUM);
        return;
    }
    if (!buff->mapped) {
        params[0] = NULL;
    } else {
        params[0] = buff->data;
    }
}

void* APIENTRY_GL4ES gl4es_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    DBG(SHUT_LOGD("glMapBufferRange(%s, %p, %zd, 0x%x)\n", PrintEnum(target), (void*)offset, length, access);)
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return NULL;
    }

    glbuffer_t* buff = getbuffer_buffer(target);
    if (buff == NULL) {
        errorShim(GL_INVALID_VALUE);
        return NULL; // Should generate an error!
    }
    if (buff->mapped) {
        errorShim(GL_INVALID_OPERATION);
        return NULL;
    }
    int need_read = (access & GL_MAP_READ_BIT) &&
                    !(access & (GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_INVALIDATE_RANGE_BIT));
    emesis_ensure_buffer_cpu_shadow(buff, target, need_read);
    buff->access = access;
    buff->mapped = 1;
    buff->ranged = 1;
    buff->offset = offset;
    buff->length = length;
    noerrorShim();
    if (!buff->data) return NULL;
    uintptr_t ret = (uintptr_t)buff->data;
    ret += offset;
    return (void*)ret;
}
void APIENTRY_GL4ES gl4es_glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    DBG(SHUT_LOGD("glFlushMappedBufferRange(%s, %p, %zd)\n", PrintEnum(target), (void*)offset, length);)
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return;
    }

    if (target == GL_ARRAY_BUFFER) VaoSharedClear(glstate->vao);

    glbuffer_t* buff = getbuffer_buffer(target);
    if (!buff) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (!buff->mapped || !buff->ranged || !(buff->access & GL_MAP_FLUSH_EXPLICIT_BIT_EXT)) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (offset < 0 || length < 0 || buff->offset + offset + length > buff->size) {
        errorShim(GL_INVALID_VALUE);
        return;
    }

    if (buff->real_buffer && buff->data && length > 0 && (buff->access & GL_MAP_WRITE_BIT_EXT)) {
        GLenum flush_target = target ? target : buff->type;
        if (flush_target == GL_ARRAY_BUFFER || flush_target == GL_ELEMENT_ARRAY_BUFFER ||
            flush_target == GL_UNIFORM_BUFFER || flush_target == GL_COPY_WRITE_BUFFER ||
            flush_target == GL_COPY_READ_BUFFER || flush_target == GL_TEXTURE_BUFFER ||
            flush_target == GL_PIXEL_PACK_BUFFER || flush_target == GL_PIXEL_UNPACK_BUFFER ||
            flush_target == GL_DRAW_INDIRECT_BUFFER) {
            GLenum gles_flush_target = (flush_target == GL_PIXEL_PACK_BUFFER ||
                                        flush_target == GL_PIXEL_UNPACK_BUFFER ||
                                        flush_target == GL_DRAW_INDIRECT_BUFFER)
                                           ? ((hardext.esversion >= 3) ? GL_COPY_READ_BUFFER : GL_ARRAY_BUFFER)
                                           : flush_target;
            LOAD_GLES(glBufferSubData);
            bindBuffer(gles_flush_target, buff->real_buffer);
            gles_glBufferSubData(gles_flush_target, buff->offset + offset, length,
                                 (void*)((uintptr_t)buff->data + buff->offset + offset));
        }
    }
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset,
                                              GLintptr writeOffset, GLsizeiptr size) {
    DBG(SHUT_LOGD("glCopyBufferSubData(%s, %s, %p, %p, %zd)\n", PrintEnum(readTarget), PrintEnum(writeTarget),
                  (void*)readOffset, (void*)writeOffset, size);)
    if (!buffer_target(readTarget) || !buffer_target(writeTarget)) {
        errorShim(GL_INVALID_ENUM);
        return;
    }

    glbuffer_t* readbuff = getbuffer_buffer(readTarget);
    glbuffer_t* writebuff = getbuffer_buffer(writeTarget);

    if (!readbuff || !writebuff) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (readOffset < 0 || writeOffset < 0 || size < 0 ||
        readOffset + size > readbuff->size || writeOffset + size > writebuff->size) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (readbuff == writebuff &&
        (readOffset + size > writeOffset && writeOffset + size > readOffset)) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (size == 0) {
        noerrorShim();
        return;
    }

    if (readbuff->gpu_dirty) emesis_sync_gpu_buffer(readbuff, GL_ARRAY_BUFFER);
    if (writebuff->gpu_dirty) emesis_sync_gpu_buffer(writebuff, GL_ARRAY_BUFFER);
    readbuff->used_by_divisor0_draw = 1;
    writebuff->used_by_divisor0_draw = 1;

    if (writebuff->data && readbuff->data) {
        memmove((char*)writebuff->data + writeOffset, (char*)readbuff->data + readOffset, (size_t)size);
    } else if (writebuff->data && !readbuff->data && !writebuff->mapped && writebuff->real_buffer) {
        free(writebuff->data);
        writebuff->data = NULL;
        writebuff->cap = 0;
    }

    if (writebuff->real_buffer) {
        typedef void (APIENTRY_GLES *glCopyBufferSubData_pfn)(GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr);
        static glCopyBufferSubData_pfn fn_gpu_copy_buf = NULL;
        static int checked_gpu_copy_buf = 0;
        if (!checked_gpu_copy_buf && hardext.esversion >= 3) {
            checked_gpu_copy_buf = 1;
            if (gles) fn_gpu_copy_buf = (glCopyBufferSubData_pfn)proc_address(gles, "glCopyBufferSubData");
        }
        if (fn_gpu_copy_buf && readbuff->real_buffer) {
            GLenum r_tgt = readTarget;
            GLenum w_tgt = writeTarget;
            if (r_tgt == w_tgt) {
                r_tgt = (w_tgt == GL_COPY_READ_BUFFER) ? GL_COPY_WRITE_BUFFER : GL_COPY_READ_BUFFER;
            }
            bindBuffer(r_tgt, readbuff->real_buffer);
            bindBuffer(w_tgt, writebuff->real_buffer);
            fn_gpu_copy_buf(r_tgt, w_tgt, readOffset, writeOffset, size);
        } else {
            emesis_ensure_buffer_cpu_shadow(readbuff, readTarget, 1);
            emesis_ensure_buffer_cpu_shadow(writebuff, writeTarget, 1);
            if (writebuff->data && readbuff->data) {
                memmove((char*)writebuff->data + writeOffset, (char*)readbuff->data + readOffset, (size_t)size);
                LOAD_GLES(glBufferSubData);
                GLenum upload_target = writeTarget ? writeTarget : writebuff->type;
                bindBuffer(upload_target, writebuff->real_buffer);
                gles_glBufferSubData(upload_target, writeOffset, size, (char*)writebuff->data + writeOffset);
            }
        }
    }

    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    LOAD_GLES(glBindBufferBase);
    if (!gles_glBindBufferBase) return;

    if (buffer == 0) {
        gles_glBindBufferBase(target, index, 0);
        noerrorShim();
        return;
    }

    glbuffer_t* buff = getbuffer_id(buffer);
    if (!buff) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (!buff->real_buffer || buff->gpu_dirty) {
        emesis_sync_gpu_buffer(buff, GL_ARRAY_BUFFER);
    }
    gles_glBindBufferBase(target, index, buff->real_buffer);
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset,
                                            GLsizeiptr size) {
    DBG(SHUT_LOGD("glBindBufferRange(%s, %u, %u, %p, %zd)\n", PrintEnum(target), index, buffer, (void*)offset, size);)

    LOAD_GLES(glBindBufferRange);
    if (!gles_glBindBufferRange) return;

    if (buffer == 0) {
        gles_glBindBufferRange(target, index, 0, offset, size);
        noerrorShim();
        return;
    }

    glbuffer_t* buff = getbuffer_id(buffer);

    if (!buff) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }

    if (!buff->real_buffer || buff->gpu_dirty) {
        emesis_sync_gpu_buffer(buff, GL_ARRAY_BUFFER);
    }

    gles_glBindBufferRange(target, index, buff->real_buffer, offset, size);

    noerrorShim();
}

GLuint APIENTRY_GL4ES gl4es_glGetUniformBlockIndex(GLuint program, const GLchar* name) {
    LOAD_GLES3(glGetUniformBlockIndex);
    if (!gles_glGetUniformBlockIndex || !name) return GL_INVALID_INDEX;

    GLuint result = gles_glGetUniformBlockIndex(program, name);
    errorGL();
    return result;
}

void APIENTRY_GL4ES gl4es_glUniformBlockBinding(GLuint program, GLuint blockIndex, GLuint binding) {
    LOAD_GLES3(glUniformBlockBinding);
    if (!gles_glUniformBlockBinding) return;

    gles_glUniformBlockBinding(program, blockIndex, binding);
    errorGL();
}

void APIENTRY_GL4ES gl4es_glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex, GLenum pname,
                                                    GLint* params) {
    if (!params) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    LOAD_GLES3(glGetActiveUniformBlockiv);
    if (!gles_glGetActiveUniformBlockiv) {
        *params = 0;
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    errorGL();
    gles_glGetActiveUniformBlockiv(program, uniformBlockIndex, pname, params);
}

void APIENTRY_GL4ES gl4es_glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex, GLsizei bufSize,
                                                      GLsizei* length, GLchar* uniformBlockName) {
    LOAD_GLES3(glGetActiveUniformBlockName);
    if (!gles_glGetActiveUniformBlockName) {
        if (length) *length = 0;
        if (uniformBlockName && bufSize > 0) uniformBlockName[0] = '\0';
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    errorGL();
    gles_glGetActiveUniformBlockName(program, uniformBlockIndex, bufSize, length, uniformBlockName);
}

void APIENTRY_GL4ES gl4es_glGetBufferParameteri64v(GLenum target, GLenum value, GLint64* data) {
    if (!data) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    if (!buffer_target(target)) {
        errorShim(GL_INVALID_ENUM);
        return;
    }
    glbuffer_t* buff = getbuffer_buffer(target);
    if (!buff) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    switch (value) {
    case GL_BUFFER_ACCESS:
        *data = (GLint64)buff->access;
        break;
    case GL_BUFFER_ACCESS_FLAGS:
        *data = (GLint64)(buff->mapping_flags ? buff->mapping_flags : (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT));
        break;
    case GL_BUFFER_MAPPED:
        *data = (buff->mapped) ? GL_TRUE : GL_FALSE;
        break;
    case GL_BUFFER_MAP_LENGTH:
        *data = (GLint64)(buff->ranged ? buff->length : (buff->mapped ? buff->size : 0));
        break;
    case GL_BUFFER_MAP_OFFSET:
        *data = (GLint64)(buff->ranged ? buff->offset : 0);
        break;
    case GL_BUFFER_SIZE:
        *data = (GLint64)buff->size;
        break;
    case GL_BUFFER_USAGE:
        *data = (GLint64)buff->usage;
        break;
    default:
        if (hardext.esversion >= 3 && buff->real_buffer) {
            typedef void (*p_glGetBufferParameteri64v_t)(GLenum, GLenum, GLint64*);
            static p_glGetBufferParameteri64v_t fn = NULL;
            static int fn_checked = 0;
            if (!fn_checked && gles) {
                fn = (p_glGetBufferParameteri64v_t)proc_address(gles, "glGetBufferParameteri64v");
                fn_checked = 1;
            }
            if (fn) {
                GLenum safe_tgt = (target == GL_PIXEL_PACK_BUFFER || target == GL_PIXEL_UNPACK_BUFFER ||
                                   target == GL_DRAW_INDIRECT_BUFFER)
                                      ? ((hardext.esversion >= 3) ? GL_COPY_WRITE_BUFFER : GL_ARRAY_BUFFER)
                                      : target;
                errorGL();
                fn(safe_tgt, value, data);
                return;
            }
        }
        errorShim(GL_INVALID_ENUM);
        return;
    }
    noerrorShim();
}

void APIENTRY_GL4ES gl4es_glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) {
    glTexBuffer(target, internalformat, buffer);
}

void bindBuffer(GLenum target, GLuint buffer) {
    DBG(SHUT_LOGD("bindBuffer(%s, %i)\n", PrintEnum(target), buffer);)
    LOAD_GLES(glBindBuffer);
    if (target == GL_ARRAY_BUFFER) {
        if (glstate->bind_buffer.array == buffer) return;
        DBG(SHUT_LOGD("Bind buffer %d to GL_ARRAY_BUFFER\n", buffer);)
        glstate->bind_buffer.array = buffer;
        gles_glBindBuffer(target, buffer);

    } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
        glstate->bind_buffer.want_index = buffer;
        if (glstate->bind_buffer.index == buffer) return;
        glstate->bind_buffer.index = buffer;
        DBG(SHUT_LOGD("Bind buffer %d to GL_ELEMENT_ARRAY_BUFFER\n", buffer);)
        gles_glBindBuffer(target, buffer);
    } else if (target == GL_COPY_READ_BUFFER) {
        if (glstate->bind_buffer.copy_read == buffer) return;
        glstate->bind_buffer.copy_read = buffer;
        DBG(SHUT_LOGD("Bind buffer %d to GL_COPY_READ_BUFFER\n", buffer);)
        gles_glBindBuffer(target, buffer);
    } else if (target == GL_COPY_WRITE_BUFFER) {
        if (glstate->bind_buffer.copy_write == buffer) return;
        glstate->bind_buffer.copy_write = buffer;
        DBG(SHUT_LOGD("Bind buffer %d to GL_COPY_WRITE_BUFFER\n", buffer);)
        gles_glBindBuffer(target, buffer);
    } else if (target == GL_UNIFORM_BUFFER) {
        if (glstate->bind_buffer.uniform == buffer) return;
        glstate->bind_buffer.uniform = buffer;
        DBG(SHUT_LOGD("Bind buffer %d to GL_UNIFORM_BUFFER\n", buffer);)
        gles_glBindBuffer(target, buffer);
    } else if (target == GL_TEXTURE_BUFFER) {
        if (glstate->bind_buffer.texture == buffer) return;
        glstate->bind_buffer.texture = buffer;
        DBG(SHUT_LOGD("Bind buffer %d to GL_TEXTURE_BUFFER\n", buffer);)
        gles_glBindBuffer(target, buffer);
    } else if (target == GL_PIXEL_PACK_BUFFER) {
        if (glstate->bind_buffer.pack == buffer) return;
        glstate->bind_buffer.pack = buffer;
        DBG(SHUT_LOGD("Bind buffer %d to GL_PIXEL_PACK_BUFFER\n", buffer);)
        gles_glBindBuffer(target, buffer);
    } else if (target == GL_PIXEL_UNPACK_BUFFER) {
        if (glstate->bind_buffer.unpack == buffer) return;
        glstate->bind_buffer.unpack = buffer;
        DBG(SHUT_LOGD("Bind buffer %d to GL_PIXEL_UNPACK_BUFFER\n", buffer);)
        gles_glBindBuffer(target, buffer);
    } else if (target == GL_DRAW_INDIRECT_BUFFER) {
        if (glstate->bind_buffer.indirect == buffer) return;
        glstate->bind_buffer.indirect = buffer;
        DBG(SHUT_LOGD("Bind buffer %d to GL_DRAW_INDIRECT_BUFFER\n", buffer);)
        gles_glBindBuffer(target, buffer);
    } else {
        LOGE("Warning, unhandled Buffer type %s in bindBuffer\n", PrintEnum(target));
        return;
    }
    glstate->bind_buffer.used = (glstate->bind_buffer.index && glstate->bind_buffer.array) ? 1 : 0;
}

GLuint wantBufferIndex(GLuint buffer) {
    GLuint ret = glstate->bind_buffer.want_index;
    glstate->bind_buffer.want_index = buffer;
    return ret;
}

void realize_bufferIndex() {
    LOAD_GLES(glBindBuffer);
    if (glstate->bind_buffer.index != glstate->bind_buffer.want_index) {
        glstate->bind_buffer.index = glstate->bind_buffer.want_index;
        gles_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glstate->bind_buffer.index);
        DBG(SHUT_LOGD("Bind buffer %d to GL_ELEMENT_ARRAY_BUFFER\n", glstate->bind_buffer.index);)
        glstate->bind_buffer.used = (glstate->bind_buffer.index && glstate->bind_buffer.array) ? 1 : 0;
    }
}

void deleteSingleBuffer(GLuint buffer) {
    LOAD_GLES(glDeleteBuffers);
    if (glstate->bind_buffer.index == buffer) glstate->bind_buffer.index = 0;
    if (glstate->bind_buffer.want_index == buffer) glstate->bind_buffer.want_index = 0;
    if (glstate->bind_buffer.array == buffer) glstate->bind_buffer.array = 0;
    if (glstate->bind_buffer.copy_read == buffer) glstate->bind_buffer.copy_read = 0;
    if (glstate->bind_buffer.copy_write == buffer) glstate->bind_buffer.copy_write = 0;
    if (glstate->bind_buffer.uniform == buffer) glstate->bind_buffer.uniform = 0;
    if (glstate->bind_buffer.texture == buffer) glstate->bind_buffer.texture = 0;
    if (glstate->bind_buffer.indirect == buffer) glstate->bind_buffer.indirect = 0;
    if (glstate->bind_buffer.pack == buffer) glstate->bind_buffer.pack = 0;
    if (glstate->bind_buffer.unpack == buffer) glstate->bind_buffer.unpack = 0;
    if (glstate->gleshard) {
        LOAD_GLES2(glDisableVertexAttribArray);
        for (int j = 0; j < hardext.maxvattrib; j++) {
            if (glstate->gleshard->vertexattrib[j].real_buffer == buffer) {
                if (glstate->gleshard->vertexattrib[j].enabled && hardext.esversion > 1 &&
                    gles_glDisableVertexAttribArray) {
                    gles_glDisableVertexAttribArray(j);
                    glstate->gleshard->vertexattrib[j].enabled = 0;
                    if (j < 32) glstate->gleshard->enabled_va_mask &= ~(1u << (unsigned)j);
                }
                glstate->gleshard->vertexattrib[j].buffer = NULL;
                glstate->gleshard->vertexattrib[j].real_buffer = 0;
                glstate->gleshard->vertexattrib[j].real_pointer = 0;
            }
        }
    }
    gles_glDeleteBuffers(1, &buffer);
}

void unboundBuffers() {
    if (!glstate->bind_buffer.used) return;
    LOAD_GLES(glBindBuffer);
    if (glstate->bind_buffer.array) {
        glstate->bind_buffer.array = 0;
        gles_glBindBuffer(GL_ARRAY_BUFFER, 0);
        DBG(SHUT_LOGD("Bind buffer %d to GL_ARRAY_BUFFER\n", 0);)
    }
    if (glstate->bind_buffer.index) {
        glstate->bind_buffer.index = 0;
        glstate->bind_buffer.want_index = 0;
        gles_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        DBG(SHUT_LOGD("Bind buffer %d to GL_ELEMENT_ARRAY_BUFFER\n", 0);)
    }
    if (glstate->bind_buffer.copy_read) {
        glstate->bind_buffer.copy_read = 0;
        gles_glBindBuffer(GL_COPY_READ_BUFFER, 0);
        DBG(SHUT_LOGD("Bind buffer %d to GL_COPY_READ_BUFFER\n", 0);)
    }
    if (glstate->bind_buffer.copy_write) {
        glstate->bind_buffer.copy_write = 0;
        gles_glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        DBG(SHUT_LOGD("Bind buffer %d to GL_COPY_WRITE_BUFFER\n", 0);)
    }
    if (glstate->bind_buffer.uniform) {
        glstate->bind_buffer.uniform = 0;
        gles_glBindBuffer(GL_UNIFORM_BUFFER, 0);
        DBG(SHUT_LOGD("Bind buffer %d to GL_UNIFORM_BUFFER\n", 0);)
    }
    if (glstate->bind_buffer.texture) {
        glstate->bind_buffer.texture = 0;
        gles_glBindBuffer(GL_TEXTURE_BUFFER, 0);
        DBG(SHUT_LOGD("Bind buffer %d to GL_TEXTURE_BUFFER\n", 0);)
    }
    if (glstate->bind_buffer.pack) {
        glstate->bind_buffer.pack = 0;
        gles_glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        DBG(SHUT_LOGD("Bind buffer %d to GL_PIXEL_PACK_BUFFER\n", 0);)
    }
    if (glstate->bind_buffer.unpack) {
        glstate->bind_buffer.unpack = 0;
        gles_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        DBG(SHUT_LOGD("Bind buffer %d to GL_PIXEL_UNPACK_BUFFER\n", 0);)
    }
    if (glstate->bind_buffer.indirect) {
        glstate->bind_buffer.indirect = 0;
        gles_glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        DBG(SHUT_LOGD("Bind buffer %d to GL_DRAW_INDIRECT_BUFFER\n", 0);)
    }
    glstate->bind_buffer.used = 0;
}

// Direct wrapper
AliasExport(void, glGenBuffers, , (GLsizei n, GLuint* buffers));
AliasExport(void, glBindBuffer, , (GLenum target, GLuint buffer));
AliasExport(void, glBufferData, , (GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage));
AliasExport(void, glBufferSubData, , (GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data));
AliasExport(void, glDeleteBuffers, , (GLsizei n, const GLuint* buffers));
AliasExport(GLboolean, glIsBuffer, , (GLuint buffer));
AliasExport(void, glGetBufferParameteriv, , (GLenum target, GLenum value, GLint* data));
AliasExport(void*, glMapBuffer, , (GLenum target, GLenum access));
AliasExport(GLboolean, glUnmapBuffer, , (GLenum target));
AliasExport(void, glGetBufferSubData, , (GLenum target, GLintptr offset, GLsizeiptr size, GLvoid* data));
AliasExport(void, glGetBufferPointerv, , (GLenum target, GLenum pname, GLvoid** params));

AliasExport(void*, glMapBufferRange, , (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access));
AliasExport(void, glFlushMappedBufferRange, , (GLenum target, GLintptr offset, GLsizeiptr length));

AliasExport(void, glCopyBufferSubData, ,
            (GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size));
AliasExport(void, glBindBufferBase, , (GLenum target, GLuint index, GLuint buffer));
AliasExport(void, glBindBufferRange, , (GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size));
AliasExport(GLuint, glGetUniformBlockIndex, , (GLuint program, const GLchar* name));
AliasExport(void, glUniformBlockBinding, , (GLuint program, GLuint blockIndex, GLuint binding));
AliasExport(void, glGetActiveUniformBlockiv, ,
            (GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint* params));
AliasExport(void, glGetActiveUniformBlockName, ,
            (GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei* length, GLchar* uniformBlockName));
AliasExport(void, glGetBufferParameteri64v, , (GLenum target, GLenum value, GLint64* data));
AliasExport(void, glTexBuffer, ARB, (GLenum target, GLenum internalformat, GLuint buffer));
AliasExport(void, glTexBuffer, EXT, (GLenum target, GLenum internalformat, GLuint buffer));
// ARB wrapper
#ifndef AMIGAOS4
AliasExport(void, glGenBuffers, ARB, (GLsizei n, GLuint* buffers));
AliasExport(void, glBindBuffer, ARB, (GLenum target, GLuint buffer));
AliasExport(void, glBufferData, ARB, (GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage));
AliasExport(void, glBufferSubData, ARB, (GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data));
AliasExport(void, glDeleteBuffers, ARB, (GLsizei n, const GLuint* buffers));
AliasExport(GLboolean, glIsBuffer, ARB, (GLuint buffer));
AliasExport(void, glGetBufferParameteriv, ARB, (GLenum target, GLenum value, GLint* data));
AliasExport(void*, glMapBuffer, ARB, (GLenum target, GLenum access));
AliasExport(GLboolean, glUnmapBuffer, ARB, (GLenum target));
AliasExport(void, glGetBufferSubData, ARB, (GLenum target, GLintptr offset, GLsizeiptr size, GLvoid* data));
AliasExport(void, glGetBufferPointerv, ARB, (GLenum target, GLenum pname, GLvoid** params));
AliasExport(void, glBindBufferBase, ARB, (GLenum target, GLuint index, GLuint buffer));
AliasExport(void, glBindBufferRange, ARB,
            (GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size));
AliasExport(GLuint, glGetUniformBlockIndex, ARB, (GLuint program, const GLchar* name));
AliasExport(void, glUniformBlockBinding, ARB, (GLuint program, GLuint blockIndex, GLuint binding));
AliasExport(void, glGetActiveUniformBlockiv, ARB,
            (GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint* params));
AliasExport(void, glGetActiveUniformBlockName, ARB,
            (GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei* length, GLchar* uniformBlockName));
#endif

// Direct Access
AliasExport(void, glNamedBufferData, , (GLuint buffer, GLsizeiptr size, const GLvoid* data, GLenum usage));
AliasExport(void, glNamedBufferSubData, , (GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid* data));
AliasExport(void, glGetNamedBufferParameteriv, , (GLuint buffer, GLenum value, GLint* data));
AliasExport(void*, glMapNamedBuffer, , (GLuint buffer, GLenum access));
AliasExport(GLboolean, glUnmapNamedBuffer, , (GLuint buffer));
AliasExport(void, glGetNamedBufferSubData, , (GLuint buffer, GLintptr offset, GLsizeiptr size, GLvoid* data));
AliasExport(void, glGetNamedBufferPointerv, , (GLuint buffer, GLenum pname, GLvoid** params));

AliasExport(void, glNamedBufferData, EXT, (GLuint buffer, GLsizeiptr size, const GLvoid* data, GLenum usage));
AliasExport(void, glNamedBufferSubData, EXT, (GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid* data));
AliasExport(void, glGetNamedBufferParameteriv, EXT, (GLuint buffer, GLenum value, GLint* data));
AliasExport(void*, glMapNamedBuffer, EXT, (GLuint buffer, GLenum access));
AliasExport(GLboolean, glUnmapNamedBuffer, EXT, (GLuint buffer));
AliasExport(void, glGetNamedBufferSubData, EXT, (GLuint buffer, GLintptr offset, GLsizeiptr size, GLvoid* data));
AliasExport(void, glGetNamedBufferPointerv, EXT, (GLuint buffer, GLenum pname, GLvoid** params));

// VAO ****************
static GLuint lastvao = 1;

void APIENTRY_GL4ES gl4es_glGenVertexArrays(GLsizei n, GLuint* arrays) {
    DBG(SHUT_LOGD("glGenVertexArrays(%i, %p)\n", n, arrays);)
    noerrorShim();
    if (n < 1) {
        errorShim(GL_INVALID_VALUE);
        return;
    }
    for (int i = 0; i < n; i++) { // TODO: create VAO here and check unicity
        arrays[i] = lastvao++;
    }
}
void APIENTRY_GL4ES gl4es_glBindVertexArray(GLuint array) {
    DBG(SHUT_LOGD("glBindVertexArray(%u)\n", array);)
    FLUSH_BEGINEND;

    khint_t k;
    int ret;
    khash_t(glvao)* list = glstate->vaos;
    // if array = 0 => unbind buffer!
    if (array == 0) {
        // unbind buffer
        glstate->vao = glstate->defaultvao;
    } else {
        // search for an existing buffer
        k = kh_get(glvao, list, array);
        glvao_t* glvao = NULL;
        if (k == kh_end(list)) {
            k = kh_put(glvao, list, array, &ret);
            glvao = kh_value(list, k) = malloc(sizeof(glvao_t));
            // new vao is binded to nothing (elements == NULL per OpenGL 3.0 spec)
            VaoInit(glvao);
            glvao->maxtex = glstate->vao->maxtex;

            // just put is number
            glvao->array = array;
        } else {
            glvao = kh_value(list, k);
        }
        if (glstate->defaultvao) {
            emesis_sync_global_vao_bindings(glvao, glstate->defaultvao);
            emesis_note_buffer_vao_ref(glstate->defaultvao->vertex, glvao);
        }
        glstate->vao = glvao;
    }

    noerrorShim();
}
void APIENTRY_GL4ES gl4es_glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    DBG(SHUT_LOGD("glDeleteVertexArrays(%i, %p)\n", n, arrays);)
    if (!glstate) return;
    FLUSH_BEGINEND;

    khash_t(glvao)* list = glstate->vaos;
    if (list) {
        khint_t k;
        glvao_t* glvao;
        for (int i = 0; i < n; i++) {
            GLuint t = arrays[i];
            if (t) { // don't allow to remove the default one
                k = kh_get(glvao, list, t);
                if (k != kh_end(list)) {
                    glvao = kh_value(list, k);
                    if (glstate->vao == glvao) {
                        glstate->vao = glstate->defaultvao;
                    }
                    VaoSharedClear(glvao);
                    kh_del(glvao, list, k);
                    free(glvao);
                }
            }
        }
    }
    noerrorShim();
}

GLboolean APIENTRY_GL4ES gl4es_glIsVertexArray(GLuint array) {
    DBG(SHUT_LOGD("glIsVertexArray(%u)\n", array);)
    if (!glstate) return GL_FALSE;
    khash_t(glvao)* list = glstate->vaos;
    khint_t k;
    noerrorShim();
    if (list) {
        k = kh_get(glvao, list, array);
        if (k != kh_end(list)) {
            return GL_TRUE;
        }
    }
    return GL_FALSE;
}

void VaoSharedClear(glvao_t* vao) {
    if (vao == NULL || vao->shared_arrays == NULL) return;
    if (!(--(*vao->shared_arrays))) {
        free(vao->vert.ptr);
        free(vao->color.ptr);
        free(vao->secondary.ptr);
        free(vao->normal.ptr);
        for (int i = 0; i < hardext.maxtex; i++)
            free(vao->tex[i].ptr);
        free(vao->shared_arrays);
    }
    vao->vert.ptr = NULL;
    vao->color.ptr = NULL;
    vao->secondary.ptr = NULL;
    vao->normal.ptr = NULL;
    for (int i = 0; i < hardext.maxtex; i++)
        vao->tex[i].ptr = NULL;
    vao->shared_arrays = NULL;
}

void VaoInit(glvao_t* vao) {
    memset(vao, 0, sizeof(glvao_t));
    for (int i = 0; i < hardext.maxvattrib; i++) {
        vao->vertexattrib[i].size = 4;
        vao->vertexattrib[i].type = GL_FLOAT;
    }
}

// Direct wrapper
AliasExport(void, glGenVertexArrays, , (GLsizei n, GLuint* arrays));
AliasExport(void, glBindVertexArray, , (GLuint array));
AliasExport(void, glDeleteVertexArrays, , (GLsizei n, const GLuint* arrays));
AliasExport(GLboolean, glIsVertexArray, , (GLuint array));