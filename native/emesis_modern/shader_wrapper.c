/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#define _GNU_SOURCE
#include "unordered_map/unordered_map.h"
#include "vgpu_shaderconv/shaderconv.h"
#include "glsl_optimizer/src/code/c_wrapper.h"
#include <GLES3/gl3.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "string_utils.h"
#include "egl.h"
#include "env.h"
#include "proc.h"

#define MAX_ATTACHED_SHADERS 8
#define MAX_TRACKED_ATTRIBS 32
#define ESSL_MEM_CACHE_SLOTS 256
#define EMESIS_PBIN_MAGIC 0x454D5042u /* "EMPB" */
#define EMESIS_CACHE_TAG "emesis_modern_v126"

typedef struct {
    GLenum shader_type;
    GLchar* source;
    uint64_t hash_lo;
    uint64_t hash_hi;
} shader_info_t;

typedef struct {
    GLuint frag_shader;
    GLuint attached_shaders[MAX_ATTACHED_SHADERS];
    GLuint attached_count;
    GLchar* colorbindings[MAX_DRAWBUFFERS];
    GLchar* attrib_bindings[MAX_TRACKED_ATTRIBS];
    uint64_t extra_attrib_hash;
} program_info_t;

typedef struct {
    uint64_t lo;
    uint64_t hi;
    char* essl;
} essl_mem_entry_t;

static pthread_mutex_t s_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static essl_mem_entry_t s_essl_mem_cache[ESSL_MEM_CACHE_SLOTS];
static int s_cache_cfg_init = 0;
static bool s_cache_enabled = true;
static char s_cache_dir[512];

static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static void hash128_init(uint64_t* lo, uint64_t* hi) {
    *lo = 14695981039346656037ULL;
    *hi = 0x6a09e667f3bcc908ULL;
}

static void hash128_update(uint64_t* lo, uint64_t* hi, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h1 = *lo;
    uint64_t h2 = *hi;
    for (size_t i = 0; i < len; i++) {
        h1 ^= (uint64_t)p[i];
        h1 *= 1099511628211ULL;
        h2 ^= (uint64_t)p[i] + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2);
    }
    *lo = h1;
    *hi = splitmix64(h2 ^ h1);
}

static void hash128_update_str(uint64_t* lo, uint64_t* hi, const char* s) {
    if (!s) s = "";
    hash128_update(lo, hi, s, strlen(s) + 1);
}

static void ensure_cache_config(void) {
    if (s_cache_cfg_init) return;
    pthread_mutex_lock(&s_cache_mutex);
    if (!s_cache_cfg_init) {
        s_cache_enabled = env_istrue_d("EMESIS_SHADER_CACHE", true);
        const char* dir = getenv("EMESIS_SHADER_CACHE_DIR");
        if (dir && dir[0] != '\0' && strlen(dir) < sizeof(s_cache_dir) - 64) {
            snprintf(s_cache_dir, sizeof(s_cache_dir), "%s", dir);
            mkdir(s_cache_dir, 0700);
        } else {
            s_cache_dir[0] = '\0';
        }
        s_cache_cfg_init = 1;
    }
    pthread_mutex_unlock(&s_cache_mutex);
}

static char* essl_cache_lookup(uint64_t lo, uint64_t hi) {
    ensure_cache_config();
    if (!s_cache_enabled) return NULL;

    size_t slot = (size_t)(lo & (ESSL_MEM_CACHE_SLOTS - 1));
    pthread_mutex_lock(&s_cache_mutex);
    if (s_essl_mem_cache[slot].essl != NULL &&
        s_essl_mem_cache[slot].lo == lo &&
        s_essl_mem_cache[slot].hi == hi) {
        char* copy = strdup(s_essl_mem_cache[slot].essl);
        pthread_mutex_unlock(&s_cache_mutex);
        return copy;
    }
    pthread_mutex_unlock(&s_cache_mutex);

    if (s_cache_dir[0] == '\0') return NULL;
    char path[600];
    snprintf(path, sizeof(path), "%s/%016llx%016llx.essl",
             s_cache_dir, (unsigned long long)lo, (unsigned long long)hi);
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long fsize = ftell(fp);
    if (fsize <= 0 || fsize > 4 * 1024 * 1024 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    char* buf = malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    if (nread != (size_t)fsize) {
        free(buf);
        return NULL;
    }
    buf[fsize] = '\0';

    pthread_mutex_lock(&s_cache_mutex);
    free(s_essl_mem_cache[slot].essl);
    s_essl_mem_cache[slot].lo = lo;
    s_essl_mem_cache[slot].hi = hi;
    s_essl_mem_cache[slot].essl = strdup(buf);
    pthread_mutex_unlock(&s_cache_mutex);
    return buf;
}

static void essl_cache_store(uint64_t lo, uint64_t hi, const char* essl) {
    ensure_cache_config();
    if (!s_cache_enabled || !essl) return;

    size_t slot = (size_t)(lo & (ESSL_MEM_CACHE_SLOTS - 1));
    pthread_mutex_lock(&s_cache_mutex);
    free(s_essl_mem_cache[slot].essl);
    s_essl_mem_cache[slot].lo = lo;
    s_essl_mem_cache[slot].hi = hi;
    s_essl_mem_cache[slot].essl = strdup(essl);
    pthread_mutex_unlock(&s_cache_mutex);

    if (s_cache_dir[0] == '\0') return;
    char tmp_path[620];
    char final_path[600];
    snprintf(final_path, sizeof(final_path), "%s/%016llx%016llx.essl",
             s_cache_dir, (unsigned long long)lo, (unsigned long long)hi);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d.%lu",
             final_path, (int)getpid(), (unsigned long)(uintptr_t)pthread_self());
    FILE* fp = fopen(tmp_path, "wb");
    if (!fp) return;
    size_t len = strlen(essl);
    if (fwrite(essl, 1, len, fp) == len && fflush(fp) == 0) {
        fclose(fp);
        if (rename(tmp_path, final_path) != 0) {
            unlink(tmp_path);
        }
    } else {
        fclose(fp);
        unlink(tmp_path);
    }
}

static void compute_program_hash(program_info_t* prog_info, uint64_t* lo, uint64_t* hi) {
    hash128_init(lo, hi);
    hash128_update_str(lo, hi, EMESIS_CACHE_TAG "_pbin");
    uint32_t ptr_bits = (uint32_t)(sizeof(void*) * 8);
    hash128_update(lo, hi, &ptr_bits, sizeof(ptr_bits));
    hash128_update_str(lo, hi, current_context->gpu_vendor);
    hash128_update_str(lo, hi, current_context->gpu_renderer);
    hash128_update_str(lo, hi, current_context->gpu_version);
    hash128_update(lo, hi, &prog_info->attached_count, sizeof(prog_info->attached_count));
    for (GLuint i = 0; i < prog_info->attached_count; i++) {
        GLuint sh_id = prog_info->attached_shaders[i];
        shader_info_t* sh = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)sh_id);
        if (sh) {
            hash128_update(lo, hi, &sh->shader_type, sizeof(sh->shader_type));
            hash128_update(lo, hi, &sh->hash_lo, sizeof(sh->hash_lo));
            hash128_update(lo, hi, &sh->hash_hi, sizeof(sh->hash_hi));
        } else {
            hash128_update(lo, hi, &sh_id, sizeof(sh_id));
        }
    }
    for (GLuint i = 0; i < MAX_DRAWBUFFERS; i++) {
        hash128_update_str(lo, hi, prog_info->colorbindings[i]);
    }
    for (GLuint i = 0; i < MAX_TRACKED_ATTRIBS; i++) {
        hash128_update_str(lo, hi, prog_info->attrib_bindings[i]);
    }
    hash128_update(lo, hi, &prog_info->extra_attrib_hash, sizeof(prog_info->extra_attrib_hash));
}

static bool try_load_program_binary(GLuint program, uint64_t lo, uint64_t hi) {
    ensure_cache_config();
    if (!s_cache_enabled || s_cache_dir[0] == '\0' || !es3_functions.glProgramBinary) return false;

    char path[600];
    snprintf(path, sizeof(path), "%s/%016llx%016llx.pbin",
             s_cache_dir, (unsigned long long)lo, (unsigned long long)hi);
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;

    uint32_t hdr[3] = {0, 0, 0};
    if (fread(hdr, sizeof(uint32_t), 3, fp) != 3 ||
        hdr[0] != EMESIS_PBIN_MAGIC ||
        hdr[2] == 0 || hdr[2] > 16u * 1024u * 1024u) {
        fclose(fp);
        unlink(path);
        return false;
    }
    void* bin = malloc(hdr[2]);
    if (!bin) {
        fclose(fp);
        return false;
    }
    if (fread(bin, 1, hdr[2], fp) != hdr[2]) {
        free(bin);
        fclose(fp);
        unlink(path);
        return false;
    }
    fclose(fp);

    es3_functions.glProgramBinary(program, (GLenum)hdr[1], bin, (GLsizei)hdr[2]);
    free(bin);
    /* Drain any error if driver invalidated binary across OS/driver update */
    while (es3_functions.glGetError() != GL_NO_ERROR) {}
    GLint link_ok = GL_FALSE;
    es3_functions.glGetProgramiv(program, GL_LINK_STATUS, &link_ok);
    if (link_ok != GL_TRUE) {
        unlink(path);
        return false;
    }
    return true;
}

static void save_program_binary(GLuint program, uint64_t lo, uint64_t hi) {
    ensure_cache_config();
    if (!s_cache_enabled || s_cache_dir[0] == '\0' || !es3_functions.glGetProgramBinary) return;

    GLint link_ok = GL_FALSE;
    es3_functions.glGetProgramiv(program, GL_LINK_STATUS, &link_ok);
    if (link_ok != GL_TRUE) return;

    GLint bin_len = 0;
    es3_functions.glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &bin_len);
    if (bin_len <= 0 || bin_len > 16 * 1024 * 1024) return;

    void* bin = malloc((size_t)bin_len);
    if (!bin) return;
    GLsizei actual_len = 0;
    GLenum bin_fmt = 0;
    es3_functions.glGetProgramBinary(program, bin_len, &actual_len, &bin_fmt, bin);
    if (es3_functions.glGetError() != GL_NO_ERROR || actual_len <= 0 || actual_len > bin_len) {
        free(bin);
        return;
    }

    char final_path[600];
    char tmp_path[620];
    snprintf(final_path, sizeof(final_path), "%s/%016llx%016llx.pbin",
             s_cache_dir, (unsigned long long)lo, (unsigned long long)hi);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d.%lu",
             final_path, (int)getpid(), (unsigned long)(uintptr_t)pthread_self());
    FILE* fp = fopen(tmp_path, "wb");
    if (!fp) {
        free(bin);
        return;
    }
    uint32_t hdr[3] = { EMESIS_PBIN_MAGIC, (uint32_t)bin_fmt, (uint32_t)actual_len };
    bool ok = (fwrite(hdr, sizeof(uint32_t), 3, fp) == 3) &&
              (fwrite(bin, 1, (size_t)actual_len, fp) == (size_t)actual_len) &&
              (fflush(fp) == 0);
    fclose(fp);
    free(bin);
    if (ok) {
        if (rename(tmp_path, final_path) != 0) {
            unlink(tmp_path);
        }
    } else {
        unlink(tmp_path);
    }
}

GLuint glCreateProgram(void) {
    emesis_ensure_current_context();
    if(!current_context) return 0;
    GLuint phys_program = es3_functions.glCreateProgram();
    if(phys_program == 0) return phys_program;
    program_info_t *prog_info = calloc(1, sizeof(program_info_t));
    if(prog_info == NULL) {
        printf("EmesisShader: failed to allocate program_info\n");
        abort();
    }
    unordered_map_put(current_context->program_map, (void*)(uintptr_t)phys_program, prog_info);
    return phys_program;
}

void glDeleteProgram(GLuint program) {
    emesis_ensure_current_context();
    if(!current_context) return;
    if (current_context->program == program) current_context->program = 0;
    es3_functions.glDeleteProgram(program);
    program_info_t *old_programinfo = unordered_map_remove(current_context->program_map, (void*)(uintptr_t)program);
    if(old_programinfo == NULL) return;
    for(GLuint i = 0; i < MAX_DRAWBUFFERS; i++) {
        free(old_programinfo->colorbindings[i]);
    }
    for(GLuint i = 0; i < MAX_TRACKED_ATTRIBS; i++) {
        free(old_programinfo->attrib_bindings[i]);
    }
    free(old_programinfo);
}

void glAttachShader(GLuint program, GLuint shader) {
    emesis_ensure_current_context();
    if(!current_context) return;
    es3_functions.glAttachShader(program, shader);
    program_info_t* program_info = unordered_map_get(current_context->program_map, (void*)(uintptr_t)program);
    shader_info_t* shader_info = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)shader);
    if(program_info == NULL) return;
    bool already = false;
    for (GLuint i = 0; i < program_info->attached_count; i++) {
        if (program_info->attached_shaders[i] == shader) {
            already = true;
            break;
        }
    }
    if (!already && program_info->attached_count < MAX_ATTACHED_SHADERS) {
        program_info->attached_shaders[program_info->attached_count++] = shader;
    }
    if(shader_info != NULL && shader_info->shader_type == GL_FRAGMENT_SHADER) {
        program_info->frag_shader = shader;
    }
}

void glDetachShader(GLuint program, GLuint shader) {
    emesis_ensure_current_context();
    if(!current_context) return;
    es3_functions.glDetachShader(program, shader);
    program_info_t* program_info = unordered_map_get(current_context->program_map, (void*)(uintptr_t)program);
    if(program_info == NULL) return;
    for (GLuint i = 0; i < program_info->attached_count; i++) {
        if (program_info->attached_shaders[i] == shader) {
            for (GLuint j = i + 1; j < program_info->attached_count; j++) {
                program_info->attached_shaders[j - 1] = program_info->attached_shaders[j];
            }
            program_info->attached_count--;
            break;
        }
    }
    if (program_info->frag_shader == shader) {
        program_info->frag_shader = 0;
    }
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
    emesis_ensure_current_context();
    if(!current_context) return;
    es3_functions.glBindAttribLocation(program, index, name);
    program_info_t *program_info = unordered_map_get(current_context->program_map, (void*)(uintptr_t)program);
    if(program_info == NULL || name == NULL) return;
    if (index < MAX_TRACKED_ATTRIBS) {
        free(program_info->attrib_bindings[index]);
        program_info->attrib_bindings[index] = strdup(name);
    } else {
        uint64_t hi = 0;
        hash128_update(&program_info->extra_attrib_hash, &hi, &index, sizeof(index));
        hash128_update_str(&program_info->extra_attrib_hash, &hi, name);
    }
}

void glBindFragDataLocation(GLuint program, GLuint colorNumber, const char * name) {
    emesis_ensure_current_context();
    if(!current_context || !name) return;
    program_info_t *program_info = unordered_map_get(current_context->program_map, (void*)(uintptr_t)program);
    if(program_info == NULL || colorNumber >= MAX_DRAWBUFFERS) return;
    free(program_info->colorbindings[colorNumber]);
    program_info->colorbindings[colorNumber] = strdup(name);
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    emesis_ensure_current_context();
    if(!current_context || !params) return;
    shader_info_t* shader_info = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)shader);
    if(shader_info != NULL && shader_info->shader_type == GL_FRAGMENT_SHADER && pname == GL_COMPILE_STATUS) {
        *params = GL_TRUE;
        return;
    }
    es3_functions.glGetShaderiv(shader, pname, params);
}

static void insert_fragout_pos(char* source, int* size, const char* name, GLuint pos) {
    char src_string[256] = { 0 };
    char dst_string[256] = { 0 };
    snprintf(src_string, sizeof(src_string), "/* EMESIS INSERT LOCATION %s EMESIS */", name);
    snprintf(dst_string, sizeof(dst_string), "layout(location = %u) ", pos);
    gl4es_inplace_replace_simple(source, size, src_string, dst_string);
}

void glLinkProgram(GLuint program) {
    emesis_ensure_current_context();
    if(!current_context) return;
    program_info_t* program_info = unordered_map_get(current_context->program_map, (void*)(uintptr_t)program);
    uint64_t prog_lo = 0, prog_hi = 0;
    bool have_prog_hash = false;
    if (program_info != NULL && program_info->attached_count > 0) {
        compute_program_hash(program_info, &prog_lo, &prog_hi);
        have_prog_hash = true;
        if (try_load_program_binary(program, prog_lo, prog_hi)) {
            return;
        }
    }

    if (es3_functions.glProgramParameteri) {
        es3_functions.glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    }

    if(program_info == NULL || program_info->frag_shader == 0) {
        goto fallthrough;
    }
    shader_info_t *shader = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)program_info->frag_shader);
    if(shader == NULL || shader->source == NULL) {
        goto fallthrough;
    }
    int nsrc_size = (int)(strlen(shader->source) + 1);
    char* new_source = malloc((size_t)nsrc_size);
    if (!new_source) goto fallthrough;
    memcpy(new_source, shader->source, (size_t)nsrc_size);
    bool changesMade = false;
    for(GLuint i = 0; i < MAX_DRAWBUFFERS; i++) {
        const char* colorbind = program_info->colorbindings[i];
        if(colorbind == NULL) continue;
        insert_fragout_pos(new_source, &nsrc_size, colorbind, i);
        changesMade = true;
    }
    if(!changesMade) {
        free(new_source);
        goto fallthrough;
    }
    const GLchar* const_source = (const GLchar*)new_source;
    GLuint patched_shader = es3_functions.glCreateShader(GL_FRAGMENT_SHADER);
    if(patched_shader == 0) {
        free(new_source);
        printf("EmesisShader: failed to initialize patched shader\n");
        goto fallthrough;
    }
    es3_functions.glShaderSource(patched_shader, 1, &const_source, NULL);
    es3_functions.glCompileShader(patched_shader);
    GLint compileStatus = GL_FALSE;
    es3_functions.glGetShaderiv(patched_shader, GL_COMPILE_STATUS, &compileStatus);
    if(compileStatus != GL_TRUE) {
        GLint logSize = 0;
        es3_functions.glGetShaderiv(patched_shader, GL_INFO_LOG_LENGTH, &logSize);
        GLchar* log = (logSize > 0) ? calloc((size_t)logSize + 1, 1) : NULL;
        if (log) {
            es3_functions.glGetShaderInfoLog(patched_shader, logSize, NULL, log);
        }
        printf("EmesisShader: failed to compile patched fragment shader, using default. Log:\n%s\n",
               log ? log : "(empty)");
        free(log);
        free(new_source);
        es3_functions.glDeleteShader(patched_shader);
        goto fallthrough;
    }
    free(new_source);
    es3_functions.glDetachShader(program, program_info->frag_shader);
    es3_functions.glAttachShader(program, patched_shader);
    es3_functions.glLinkProgram(program);
    es3_functions.glDetachShader(program, patched_shader);
    es3_functions.glAttachShader(program, program_info->frag_shader);
    es3_functions.glDeleteShader(patched_shader);
    if (have_prog_hash) {
        save_program_binary(program, prog_lo, prog_hi);
    }
    return;

fallthrough:
    es3_functions.glLinkProgram(program);
    if (have_prog_hash) {
        save_program_binary(program, prog_lo, prog_hi);
    }
}

GLuint glCreateShader(GLenum shaderType) {
    emesis_ensure_current_context();
    if(!current_context) return 0;
    GLuint phys_shader = es3_functions.glCreateShader(shaderType);
    if(phys_shader == 0) return 0;
    shader_info_t* info_struct = calloc(1, sizeof(shader_info_t));
    if(info_struct == NULL) {
        printf("EmesisShader: failed to allocate shader_info\n");
        abort();
    }
    info_struct->shader_type = shaderType;
    unordered_map_put(current_context->shader_map, (void*)(uintptr_t)phys_shader, info_struct);
    return phys_shader;
}

void glDeleteShader(GLuint shader) {
    emesis_ensure_current_context();
    if(!current_context) return;
    es3_functions.glDeleteShader(shader);
    shader_info_t * old_shaderinfo = unordered_map_remove(current_context->shader_map, (void*)(uintptr_t)shader);
    if(old_shaderinfo == NULL) return;
    free(old_shaderinfo->source);
    free(old_shaderinfo);
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) {
    emesis_ensure_current_context();
    if(!current_context || count <= 0 || !string) return;
    shader_info_t* shader_info = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)shader);
    if(shader_info == NULL) {
        es3_functions.glShaderSource(shader, count, string, length);
        return;
    }

    size_t target_length = 0;
    for(GLsizei i = 0; i < count; i++) {
        if (!string[i]) continue;
        size_t part_len = (length != NULL && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
        target_length += part_len;
    }
    GLchar* target_string = malloc((target_length + 1) * sizeof(GLchar));
    if (!target_string) return;
    size_t offset = 0;
    for(GLsizei i = 0; i < count; i++) {
        if (!string[i]) continue;
        size_t part_len = (length != NULL && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
        memcpy(&target_string[offset], string[i], part_len);
        offset += part_len;
    }
    target_string[offset] = '\0';

    uint64_t h_lo = 0, h_hi = 0;
    hash128_init(&h_lo, &h_hi);
    hash128_update_str(&h_lo, &h_hi, EMESIS_CACHE_TAG "_essl");
    hash128_update(&h_lo, &h_hi, &shader_info->shader_type, sizeof(shader_info->shader_type));
    hash128_update(&h_lo, &h_hi, &current_context->shader_version, sizeof(current_context->shader_version));
    hash128_update(&h_lo, &h_hi, target_string, offset);
    shader_info->hash_lo = h_lo;
    shader_info->hash_hi = h_hi;

    GLchar* new_source = essl_cache_lookup(h_lo, h_hi);
    if (!new_source) {
        new_source = optimize_shader(target_string, shader_info->shader_type, 460, current_context->shader_version);
        if (new_source) {
            essl_cache_store(h_lo, h_hi, new_source);
        }
    }
    free(shader_info->source);
    shader_info->source = NULL;
    if(!new_source) {
        printf("EmesisShader: failed to convert&optimize shader %u, falling back to raw source\n", shader);
        const GLchar* raw_ptr = target_string;
        es3_functions.glShaderSource(shader, 1, &raw_ptr, NULL);
        free(target_string);
        return;
    }
    shader_info->source = new_source;
    const GLchar* converted_ptr = shader_info->source;
    es3_functions.glShaderSource(shader, 1, &converted_ptr, NULL);
    free(target_string);
}
