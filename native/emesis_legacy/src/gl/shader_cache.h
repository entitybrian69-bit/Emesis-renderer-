#ifndef _EMESIS_SHADER_CACHE_H_
#define _EMESIS_SHADER_CACHE_H_

#include <stdint.h>
#include <stddef.h>
#include "gles.h"
#include "shader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t h1;
    uint64_t h2;
} emesis_hash128_t;

struct program_s;

void emesis_shader_cache_init(void);

emesis_hash128_t emesis_shader_compute_key(GLenum shader_type,
                                           int target_essl,
                                           const shaderconv_need_t* need,
                                           const char* glsl_source);

int emesis_shader_cache_lookup(const emesis_hash128_t* key,
                               char** out_converted,
                               int* out_is_essl_320,
                               shaderconv_need_t* out_need,
                               uniform_declarations_list_t* out_decls);

void emesis_shader_cache_store(const emesis_hash128_t* key,
                               const char* converted,
                               int is_essl_320,
                               const shaderconv_need_t* need,
                               const uniform_declarations_list_t* decls);

int emesis_program_cache_try_load(GLuint gl_program_id,
                                  const char* vert_essl,
                                  const char* frag_essl,
                                  const char* comp_essl,
                                  const char* attrib_bindings_sig);

void emesis_program_cache_store(GLuint gl_program_id,
                                const char* vert_essl,
                                const char* frag_essl,
                                const char* comp_essl,
                                const char* attrib_bindings_sig);

#ifdef __cplusplus
}
#endif

#endif /* _EMESIS_SHADER_CACHE_H_ */
