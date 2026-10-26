#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "shader_cache.h"
#include "../glx/hardext.h"
#include "glsl/glsl_for_es.h"
#include "init.h"
#include "loader.h"
#include "logs.h"
#include "version.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define EMESIS_SC_SCHEMA_VERSION                                                                                       \
    (0x454D534300020000ULL | ((uint64_t)(MAJOR & 0xFF) << 12) | ((uint64_t)(MINOR & 0x3F) << 6) |                      \
     (uint64_t)(REVISION & 0x3F))
#define EMESIS_PC_SCHEMA_VERSION                                                                                       \
    (0x454D504200020000ULL | ((uint64_t)(MAJOR & 0xFF) << 12) | ((uint64_t)(MINOR & 0x3F) << 6) |                      \
     (uint64_t)(REVISION & 0x3F))

#define EMESIS_SC_MAGIC 0x43534D45U /* 'EMSC' */
#define EMESIS_PC_MAGIC 0x42504D45U /* 'EMPB' */

#define EMESIS_MEM_SHADER_SLOTS 32
#define EMESIS_MEM_PROGBIN_SLOTS 16
#define EMESIS_MAX_MEM_PROGBIN_ENTRY_BYTES (256 * 1024)
#define EMESIS_MAX_DISK_SHADER_BYTES (4 * 1024 * 1024)
#define EMESIS_MAX_DISK_PROGBIN_BYTES (16 * 1024 * 1024)

typedef struct {
    int valid;
    emesis_hash128_t key;
    char* converted;
    int is_essl_320;
    shaderconv_need_t need;
    uniform_declarations_list_t decls;
} mem_shader_entry_t;

typedef struct {
    int valid;
    emesis_hash128_t key;
    GLenum format;
    uint32_t length;
    void* binary;
} mem_progbin_entry_t;

static pthread_mutex_t g_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_cache_inited = 0;
static int g_cache_enabled = 1;
static int g_disk_enabled = 0;
static char g_cache_dir[1024] = {0};
static emesis_hash128_t g_driver_fp = {0, 0};
static int g_driver_fp_ready = 0;
static mem_shader_entry_t g_mem_shaders[EMESIS_MEM_SHADER_SLOTS];
static mem_progbin_entry_t g_mem_progbins[EMESIS_MEM_PROGBIN_SLOTS];
static volatile uint32_t g_compiler_dirty_count = 0;
static volatile uint32_t g_quiet_clears_since_compile = 0;

static inline uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void hash128_init(emesis_hash128_t* h, uint64_t seed) {
    h->h1 = 0xcbf29ce484222325ULL ^ mix64(seed);
    h->h2 = 0x84222325cbf29ce4ULL ^ mix64(seed ^ 0x9e3779b97f4a7c15ULL);
}

static void hash128_update(emesis_hash128_t* h, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h1 = h->h1;
    uint64_t h2 = h->h2;
    size_t i = 0;
    while (i + 8 <= len) {
        uint64_t w;
        memcpy(&w, p + i, sizeof(w));
        h1 = mix64(h1 ^ w);
        h2 = (h2 ^ mix64(w + i)) * 0x100000001b3ULL;
        i += 8;
    }
    for (; i < len; ++i) {
        h1 ^= (uint64_t)p[i];
        h1 *= 0x100000001b3ULL;
        h2 ^= ((uint64_t)p[i] << ((i & 7) * 8));
        h2 = mix64(h2);
    }
    h->h1 = mix64(h1 ^ (uint64_t)len);
    h->h2 = mix64(h2 ^ ((uint64_t)len * 0x9e3779b97f4a7c15ULL));
}

static void hash128_update_str(emesis_hash128_t* h, const char* s) {
    if (!s) {
        uint32_t zero = 0;
        hash128_update(h, &zero, sizeof(zero));
        return;
    }
    size_t len = strlen(s);
    hash128_update(h, &len, sizeof(len));
    hash128_update(h, s, len);
}

static int mkdir_p(const char* path) {
    if (!path || !path[0]) return -1;
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    if (len > 1 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                /* keep trying deeper components in case parent is non-writable root like /sdcard */
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0775) != 0 && errno != EEXIST) return -1;
    return access(tmp, W_OK | X_OK);
}

static void purge_stale_cache_files(const char* dir_path) {
    if (!dir_path || !dir_path[0]) return;
    char marker_path[1152];
    snprintf(marker_path, sizeof(marker_path), "%s/.emesis_schema", dir_path);
    uint64_t expected_sc = EMESIS_SC_SCHEMA_VERSION;
    uint64_t expected_pc = EMESIS_PC_SCHEMA_VERSION;
    FILE* mfp = fopen(marker_path, "rb");
    if (mfp) {
        uint64_t got_sc = 0, got_pc = 0;
        int match = (fread(&got_sc, 1, sizeof(got_sc), mfp) == sizeof(got_sc) &&
                     fread(&got_pc, 1, sizeof(got_pc), mfp) == sizeof(got_pc) &&
                     got_sc == expected_sc && got_pc == expected_pc);
        fclose(mfp);
        if (match) return;
    }

    DIR* dp = opendir(dir_path);
    if (dp) {
        struct dirent* de;
        while ((de = readdir(dp)) != NULL) {
            const char* nm = de->d_name;
            if (strncmp(nm, "shdr_", 5) == 0 || strncmp(nm, "pbin_", 5) == 0) {
                char fpath[1280];
                if (snprintf(fpath, sizeof(fpath), "%s/%s", dir_path, nm) < (int)sizeof(fpath)) {
                    unlink(fpath);
                }
            }
        }
        closedir(dp);
    }

    mfp = fopen(marker_path, "wb");
    if (mfp) {
        (void)fwrite(&expected_sc, 1, sizeof(expected_sc), mfp);
        (void)fwrite(&expected_pc, 1, sizeof(expected_pc), mfp);
        fclose(mfp);
    }
}

static int try_set_cache_dir(const char* base, const char* suffix) {
    if (!base || !base[0]) return 0;
    char candidate[1024];
    int n = snprintf(candidate, sizeof(candidate), "%s%s", base, suffix ? suffix : "");
    if (n <= 0 || (size_t)n >= sizeof(candidate)) return 0;
    if (mkdir_p(candidate) == 0) {
        memcpy(g_cache_dir, candidate, (size_t)n + 1);
        g_disk_enabled = 1;
        purge_stale_cache_files(g_cache_dir);
        return 1;
    }
    return 0;
}

static void init_locked(void) {
    if (g_cache_inited) return;
    g_cache_inited = 1;

    const char* disable = getenv("EMESIS_DISABLE_SHADER_CACHE");
    if (disable && (strcmp(disable, "1") == 0 || strcasecmp(disable, "true") == 0)) {
        g_cache_enabled = 0;
        return;
    }

    const char* explicit_dir = getenv("EMESIS_SHADER_CACHE_DIR");
    if (explicit_dir && try_set_cache_dir(explicit_dir, "")) return;

    if (EmesisDirectory && EmesisDirectory[0] && try_set_cache_dir(EmesisDirectory, "/shader_cache")) return;

    const char* pojav_game_dir = getenv("POJAV_GAME_DIR");
    if (pojav_game_dir && try_set_cache_dir(pojav_game_dir, "/.emesis_shader_cache")) return;

    const char* mc_dir = getenv("MC_DIR");
    if (mc_dir && try_set_cache_dir(mc_dir, "/.emesis_shader_cache")) return;

    char cwd_buf[1024];
    if (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL && strcmp(cwd_buf, "/") != 0) {
        if (try_set_cache_dir(cwd_buf, "/.emesis_shader_cache")) return;
    }

    const char* home = getenv("HOME");
    if (home && try_set_cache_dir(home, "/.emesis_shader_cache")) return;

    const char* tmpdir = getenv("TMPDIR");
    if (tmpdir && try_set_cache_dir(tmpdir, "/emesis_shader_cache")) return;

    if (try_set_cache_dir("/sdcard/games/emesis_renderer", "/shader_cache")) return;
    (void)try_set_cache_dir("/tmp", "/emesis_shader_cache");
}

void emesis_shader_cache_init(void) {
    pthread_mutex_lock(&g_cache_mutex);
    init_locked();
    pthread_mutex_unlock(&g_cache_mutex);
}

emesis_hash128_t emesis_shader_compute_key(GLenum shader_type,
                                           int target_essl,
                                           const shaderconv_need_t* need,
                                           const char* glsl_source) {
    emesis_hash128_t h;
    hash128_init(&h, EMESIS_SC_SCHEMA_VERSION);
    uint32_t st = (uint32_t)shader_type;
    int32_t essl = (int32_t)target_essl;
    int32_t highp = (int32_t)hardext.highp;
    int32_t maxvarying = (int32_t)hardext.maxvarying;
    int32_t notexarray = (int32_t)globals4es.notexarray;
    uint32_t glsl_caps = ((uint32_t)(hardext.glsl320es & 1) << 0) |
                         ((uint32_t)(hardext.glsl310es & 1) << 1) |
                         ((uint32_t)(hardext.glsl300es & 1) << 2) |
                         ((uint32_t)(hardext.glsl120 & 1) << 3) |
                         ((uint32_t)(hardext.esversion & 0xFF) << 8);
    hash128_update(&h, &st, sizeof(st));
    hash128_update(&h, &essl, sizeof(essl));
    hash128_update(&h, &highp, sizeof(highp));
    hash128_update(&h, &maxvarying, sizeof(maxvarying));
    hash128_update(&h, &notexarray, sizeof(notexarray));
    hash128_update(&h, &glsl_caps, sizeof(glsl_caps));
    if (need) {
        hash128_update(&h, need, sizeof(*need));
    }
    hash128_update_str(&h, glsl_source);
    return h;
}

static void free_mem_entry(mem_shader_entry_t* e) {
    if (!e->valid) return;
    free(e->converted);
    e->converted = NULL;
    uniform_decls_clear(&e->decls);
    e->valid = 0;
}

static int read_exact(FILE* fp, void* buf, size_t n) {
    return fread(buf, 1, n, fp) == n ? 1 : 0;
}

static int write_exact(FILE* fp, const void* buf, size_t n) {
    return fwrite(buf, 1, n, fp) == n ? 1 : 0;
}

int emesis_shader_cache_lookup(const emesis_hash128_t* key,
                               char** out_converted,
                               int* out_is_essl_320,
                               shaderconv_need_t* out_need,
                               uniform_declarations_list_t* out_decls) {
    if (!key || !out_converted) return 0;

    pthread_mutex_lock(&g_cache_mutex);
    if (!g_cache_inited) init_locked();
    if (!g_cache_enabled) {
        pthread_mutex_unlock(&g_cache_mutex);
        return 0;
    }

    size_t slot = (size_t)(key->h1 & (EMESIS_MEM_SHADER_SLOTS - 1));
    mem_shader_entry_t* me = &g_mem_shaders[slot];
    if (me->valid && me->key.h1 == key->h1 && me->key.h2 == key->h2 && me->converted) {
        *out_converted = strdup(me->converted);
        if (out_is_essl_320) *out_is_essl_320 = me->is_essl_320;
        if (out_need) *out_need = me->need;
        if (out_decls) {
            uniform_decls_clear(out_decls);
            uniform_decls_merge(out_decls, &me->decls);
        }
        pthread_mutex_unlock(&g_cache_mutex);
        return *out_converted != NULL;
    }

    if (!g_disk_enabled) {
        pthread_mutex_unlock(&g_cache_mutex);
        return 0;
    }

    char path[1152];
    snprintf(path, sizeof(path), "%s/essl_%016llx_%016llx.ec1", g_cache_dir,
             (unsigned long long)key->h1, (unsigned long long)key->h2);
    pthread_mutex_unlock(&g_cache_mutex);

    FILE* fp = fopen(path, "rb");
    if (!fp) return 0;

    uint32_t magic = 0;
    uint64_t k1 = 0, k2 = 0;
    int32_t is_320 = 0;
    shaderconv_need_t need = {0};
    uint32_t essl_len = 0;
    uint32_t u_count = 0;

    if (!read_exact(fp, &magic, sizeof(magic)) || magic != EMESIS_SC_MAGIC ||
        !read_exact(fp, &k1, sizeof(k1)) || !read_exact(fp, &k2, sizeof(k2)) ||
        k1 != key->h1 || k2 != key->h2 ||
        !read_exact(fp, &is_320, sizeof(is_320)) ||
        !read_exact(fp, &need, sizeof(need)) ||
        !read_exact(fp, &essl_len, sizeof(essl_len)) || essl_len == 0 || essl_len > EMESIS_MAX_DISK_SHADER_BYTES ||
        !read_exact(fp, &u_count, sizeof(u_count)) || u_count > 4096) {
        fclose(fp);
        return 0;
    }

    char* conv = (char*)malloc((size_t)essl_len + 1);
    if (!conv) {
        fclose(fp);
        return 0;
    }
    if (!read_exact(fp, conv, essl_len)) {
        free(conv);
        fclose(fp);
        return 0;
    }
    conv[essl_len] = '\0';

    uniform_declarations_list_t tmp_decls = {0};
    for (uint32_t i = 0; i < u_count; ++i) {
        uint32_t vlen = 0, ilen = 0;
        if (!read_exact(fp, &vlen, sizeof(vlen)) || vlen > 2048 ||
            !read_exact(fp, &ilen, sizeof(ilen)) || ilen > 4096) {
            uniform_decls_clear(&tmp_decls);
            free(conv);
            fclose(fp);
            return 0;
        }
        char* var = (char*)malloc((size_t)vlen + 1);
        char* val = (char*)malloc((size_t)ilen + 1);
        if (!var || !val ||
            (vlen > 0 && !read_exact(fp, var, vlen)) ||
            (ilen > 0 && !read_exact(fp, val, ilen))) {
            free(var);
            free(val);
            uniform_decls_clear(&tmp_decls);
            free(conv);
            fclose(fp);
            return 0;
        }
        var[vlen] = '\0';
        val[ilen] = '\0';
        uniform_decls_add(&tmp_decls, var, val);
        free(var);
        free(val);
    }
    fclose(fp);

    /* Populate RAM slot */
    pthread_mutex_lock(&g_cache_mutex);
    free_mem_entry(me);
    me->valid = 1;
    me->key = *key;
    me->converted = strdup(conv);
    me->is_essl_320 = is_320;
    me->need = need;
    uniform_decls_merge(&me->decls, &tmp_decls);
    pthread_mutex_unlock(&g_cache_mutex);

    *out_converted = conv;
    if (out_is_essl_320) *out_is_essl_320 = is_320;
    if (out_need) *out_need = need;
    if (out_decls) {
        uniform_decls_clear(out_decls);
        uniform_decls_merge(out_decls, &tmp_decls);
    }
    uniform_decls_clear(&tmp_decls);
    return 1;
}

void emesis_shader_cache_store(const emesis_hash128_t* key,
                               const char* converted,
                               int is_essl_320,
                               const shaderconv_need_t* need,
                               const uniform_declarations_list_t* decls) {
    if (!key || !converted || !converted[0]) return;
    size_t essl_len = strlen(converted);
    if (essl_len > EMESIS_MAX_DISK_SHADER_BYTES) return;

    pthread_mutex_lock(&g_cache_mutex);
    if (!g_cache_inited) init_locked();
    if (!g_cache_enabled) {
        pthread_mutex_unlock(&g_cache_mutex);
        return;
    }

    size_t slot = (size_t)(key->h1 & (EMESIS_MEM_SHADER_SLOTS - 1));
    mem_shader_entry_t* me = &g_mem_shaders[slot];
    free_mem_entry(me);
    me->valid = 1;
    me->key = *key;
    me->converted = strdup(converted);
    me->is_essl_320 = is_essl_320;
    if (need) me->need = *need;
    if (decls) uniform_decls_merge(&me->decls, decls);

    int write_disk = g_disk_enabled;
    char dir_copy[1024];
    if (write_disk) memcpy(dir_copy, g_cache_dir, sizeof(dir_copy));
    pthread_mutex_unlock(&g_cache_mutex);

    if (!write_disk) return;

    char final_path[1152];
    char tmp_path[1184];
    snprintf(final_path, sizeof(final_path), "%s/essl_%016llx_%016llx.ec1", dir_copy,
             (unsigned long long)key->h1, (unsigned long long)key->h2);
    if (access(final_path, F_OK) == 0) return;

    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d.%lu", final_path, (int)getpid(), (unsigned long)pthread_self());
    FILE* fp = fopen(tmp_path, "wb");
    if (!fp) return;

    uint32_t magic = EMESIS_SC_MAGIC;
    int32_t is_320 = (int32_t)is_essl_320;
    shaderconv_need_t need_val = {0};
    if (need) need_val = *need;
    uint32_t len32 = (uint32_t)essl_len;
    uint32_t u_count = (decls && decls->count > 0) ? (uint32_t)decls->count : 0;

    int ok = write_exact(fp, &magic, sizeof(magic)) &&
             write_exact(fp, &key->h1, sizeof(key->h1)) &&
             write_exact(fp, &key->h2, sizeof(key->h2)) &&
             write_exact(fp, &is_320, sizeof(is_320)) &&
             write_exact(fp, &need_val, sizeof(need_val)) &&
             write_exact(fp, &len32, sizeof(len32)) &&
             write_exact(fp, &u_count, sizeof(u_count)) &&
             write_exact(fp, converted, essl_len);

    for (uint32_t i = 0; ok && i < u_count; ++i) {
        const char* var = decls->items[i].variable ? decls->items[i].variable : "";
        const char* val = decls->items[i].initial_value ? decls->items[i].initial_value : "";
        uint32_t vlen = (uint32_t)strlen(var);
        uint32_t ilen = (uint32_t)strlen(val);
        ok = write_exact(fp, &vlen, sizeof(vlen)) &&
             write_exact(fp, &ilen, sizeof(ilen)) &&
             (vlen == 0 || write_exact(fp, var, vlen)) &&
             (ilen == 0 || write_exact(fp, val, ilen));
    }

    fclose(fp);
    if (ok) {
        if (rename(tmp_path, final_path) != 0) unlink(tmp_path);
    } else {
        unlink(tmp_path);
    }
}

static int is_external_gles_fn_sc(const void* cand, const void* self_anchor) {
    if (!cand || cand == self_anchor) return 0;
    Dl_info self_info, cand_info;
    if (dladdr(self_anchor, &self_info) && dladdr(cand, &cand_info)) {
        if (cand_info.dli_fbase && cand_info.dli_fbase == self_info.dli_fbase) return 0;
        if (cand_info.dli_fname && strstr(cand_info.dli_fname, "emesis") != NULL) return 0;
    }
    return 1;
}

static glProgramBinary_PTR resolve_glProgramBinary(void) {
    static glProgramBinary_PTR fn = NULL;
    static int checked = 0;
    if (!checked) {
        checked = 1;
        const char* names[2] = {"glProgramBinary", "glProgramBinaryOES"};
        for (int i = 0; i < 2 && !fn; ++i) {
            if (gles) {
                void* p = proc_address(gles, names[i]);
                if (is_external_gles_fn_sc(p, (const void*)&resolve_glProgramBinary)) {
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
                    if (is_external_gles_fn_sc(p, (const void*)&resolve_glProgramBinary)) {
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

static glGetProgramBinary_PTR resolve_glGetProgramBinary(void) {
    static glGetProgramBinary_PTR fn = NULL;
    static int checked = 0;
    if (!checked) {
        checked = 1;
        const char* names[2] = {"glGetProgramBinary", "glGetProgramBinaryOES"};
        for (int i = 0; i < 2 && !fn; ++i) {
            if (gles) {
                void* p = proc_address(gles, names[i]);
                if (is_external_gles_fn_sc(p, (const void*)&resolve_glGetProgramBinary)) {
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
                    if (is_external_gles_fn_sc(p, (const void*)&resolve_glGetProgramBinary)) {
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

static emesis_hash128_t compute_program_key(const char* vert_essl,
                                            const char* frag_essl,
                                            const char* comp_essl,
                                            const char* attrib_bindings_sig) {
    pthread_mutex_lock(&g_cache_mutex);
    if (!g_cache_inited) init_locked();
    if (!g_driver_fp_ready) {
        LOAD_GLES(glGetString);
        const char* vendor = gles_glGetString ? (const char*)gles_glGetString(GL_VENDOR) : "";
        const char* renderer = gles_glGetString ? (const char*)gles_glGetString(GL_RENDERER) : "";
        const char* version = gles_glGetString ? (const char*)gles_glGetString(GL_VERSION) : "";
        hash128_init(&g_driver_fp, EMESIS_PC_SCHEMA_VERSION);
        hash128_update_str(&g_driver_fp, vendor);
        hash128_update_str(&g_driver_fp, renderer);
        hash128_update_str(&g_driver_fp, version);
        g_driver_fp_ready = 1;
    }
    emesis_hash128_t h = g_driver_fp;
    pthread_mutex_unlock(&g_cache_mutex);

    hash128_update_str(&h, vert_essl);
    hash128_update_str(&h, frag_essl);
    hash128_update_str(&h, comp_essl);
    hash128_update_str(&h, attrib_bindings_sig);
    return h;
}

static void store_mem_progbin_locked(const emesis_hash128_t* key, GLenum fmt, const void* binary, uint32_t len) {
    if (!key || !binary || len == 0 || len > EMESIS_MAX_MEM_PROGBIN_ENTRY_BYTES) return;
    size_t slot = (size_t)(key->h1 & (EMESIS_MEM_PROGBIN_SLOTS - 1));
    mem_progbin_entry_t* pe = &g_mem_progbins[slot];
    if (pe->valid && pe->key.h1 == key->h1 && pe->key.h2 == key->h2) return;
    void* copy = malloc(len);
    if (!copy) return;
    memcpy(copy, binary, len);
    if (pe->binary) free(pe->binary);
    pe->valid = 1;
    pe->key = *key;
    pe->format = fmt;
    pe->length = len;
    pe->binary = copy;
}

int emesis_program_cache_try_load(GLuint gl_program_id,
                                  const char* vert_essl,
                                  const char* frag_essl,
                                  const char* comp_essl,
                                  const char* attrib_bindings_sig) {
    if (!gl_program_id || !hardext.prgbinary || hardext.prgbin_n <= 0 || (!vert_essl && !frag_essl && !comp_essl))
        return 0;

    glProgramBinary_PTR fn_prog_bin = resolve_glProgramBinary();
    LOAD_GLES2(glGetProgramiv);
    LOAD_GLES(glGetError);
    if (!fn_prog_bin || !gles_glGetProgramiv) return 0;

    pthread_mutex_lock(&g_cache_mutex);
    if (!g_cache_inited) init_locked();
    if (!g_cache_enabled) {
        pthread_mutex_unlock(&g_cache_mutex);
        return 0;
    }
    int disk_ok = g_disk_enabled;
    char dir_copy[1024];
    if (disk_ok) memcpy(dir_copy, g_cache_dir, sizeof(dir_copy));
    pthread_mutex_unlock(&g_cache_mutex);

    emesis_hash128_t key = compute_program_key(vert_essl, frag_essl, comp_essl, attrib_bindings_sig);

    /* 1. Try RAM program binary cache first (deduplicates repeated ProgramTable fallback links in <1ms) */
    {
        void* mem_bin = NULL;
        uint32_t mem_len = 0;
        GLenum mem_fmt = 0;
        pthread_mutex_lock(&g_cache_mutex);
        size_t slot = (size_t)(key.h1 & (EMESIS_MEM_PROGBIN_SLOTS - 1));
        mem_progbin_entry_t* pe = &g_mem_progbins[slot];
        if (pe->valid && pe->key.h1 == key.h1 && pe->key.h2 == key.h2 && pe->binary && pe->length > 0) {
            mem_bin = malloc(pe->length);
            if (mem_bin) {
                memcpy(mem_bin, pe->binary, pe->length);
                mem_len = pe->length;
                mem_fmt = pe->format;
            }
        }
        pthread_mutex_unlock(&g_cache_mutex);

        if (mem_bin) {
            while (gles_glGetError() != GL_NO_ERROR) {}
            fn_prog_bin(gl_program_id, mem_fmt, mem_bin, (GLsizei)mem_len);
            GLenum bin_err = gles_glGetError();
            free(mem_bin);
            GLint linked = 0;
            gles_glGetProgramiv(gl_program_id, GL_LINK_STATUS, &linked);
            while (gles_glGetError() != GL_NO_ERROR) {}
            if (bin_err == GL_NO_ERROR && linked == GL_TRUE) return 1;
            pthread_mutex_lock(&g_cache_mutex);
            if (pe->valid && pe->key.h1 == key.h1 && pe->key.h2 == key.h2) {
                free(pe->binary);
                pe->binary = NULL;
                pe->valid = 0;
            }
            pthread_mutex_unlock(&g_cache_mutex);
        }
    }

    if (!disk_ok) return 0;

    char path[1152];
    snprintf(path, sizeof(path), "%s/pbin_%016llx_%016llx.ep1", dir_copy,
             (unsigned long long)key.h1, (unsigned long long)key.h2);

    FILE* fp = fopen(path, "rb");
    if (!fp) return 0;

    uint32_t magic = 0;
    uint64_t k1 = 0, k2 = 0;
    uint32_t fmt = 0;
    uint32_t bin_len = 0;
    if (!read_exact(fp, &magic, sizeof(magic)) || magic != EMESIS_PC_MAGIC ||
        !read_exact(fp, &k1, sizeof(k1)) || !read_exact(fp, &k2, sizeof(k2)) ||
        k1 != key.h1 || k2 != key.h2 ||
        !read_exact(fp, &fmt, sizeof(fmt)) ||
        !read_exact(fp, &bin_len, sizeof(bin_len)) || bin_len == 0 || bin_len > EMESIS_MAX_DISK_PROGBIN_BYTES) {
        fclose(fp);
        return 0;
    }

    void* binary = malloc(bin_len);
    if (!binary || !read_exact(fp, binary, bin_len)) {
        free(binary);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    while (gles_glGetError() != GL_NO_ERROR) {}
    fn_prog_bin(gl_program_id, (GLenum)fmt, binary, (GLsizei)bin_len);
    GLenum bin_err = gles_glGetError();

    GLint linked = 0;
    gles_glGetProgramiv(gl_program_id, GL_LINK_STATUS, &linked);
    while (gles_glGetError() != GL_NO_ERROR) {}
    if (bin_err != GL_NO_ERROR || linked != GL_TRUE) {
        free(binary);
        /* Driver updated or rejected binary; purge stale cache file so fresh link replaces it */
        unlink(path);
        return 0;
    }

    pthread_mutex_lock(&g_cache_mutex);
    store_mem_progbin_locked(&key, (GLenum)fmt, binary, bin_len);
    pthread_mutex_unlock(&g_cache_mutex);
    free(binary);
    return 1;
}

void emesis_program_cache_store(GLuint gl_program_id,
                                const char* vert_essl,
                                const char* frag_essl,
                                const char* comp_essl,
                                const char* attrib_bindings_sig) {
    if (!gl_program_id || !hardext.prgbinary || hardext.prgbin_n <= 0 || (!vert_essl && !frag_essl && !comp_essl))
        return;

    pthread_mutex_lock(&g_cache_mutex);
    if (!g_cache_inited) init_locked();
    if (!g_cache_enabled) {
        pthread_mutex_unlock(&g_cache_mutex);
        return;
    }
    int disk_ok = g_disk_enabled;
    char dir_copy[1024];
    if (disk_ok) memcpy(dir_copy, g_cache_dir, sizeof(dir_copy));
    pthread_mutex_unlock(&g_cache_mutex);

    emesis_hash128_t key = compute_program_key(vert_essl, frag_essl, comp_essl, attrib_bindings_sig);

    glGetProgramBinary_PTR fn_get_bin = resolve_glGetProgramBinary();
    LOAD_GLES2(glGetProgramiv);
    LOAD_GLES(glGetError);
    if (!fn_get_bin || !gles_glGetProgramiv) return;

    GLint buf_len = 0;
    gles_glGetProgramiv(gl_program_id, GL_PROGRAM_BINARY_LENGTH_OES, &buf_len);
    if (buf_len <= 0 || buf_len > EMESIS_MAX_DISK_PROGBIN_BYTES) {
        while (gles_glGetError() != GL_NO_ERROR) {}
        return;
    }

    void* binary = malloc((size_t)buf_len);
    if (!binary) return;

    GLsizei actual_len = 0;
    GLenum format = 0;
    fn_get_bin(gl_program_id, buf_len, &actual_len, &format, binary);
    if (gles_glGetError() != GL_NO_ERROR || actual_len <= 0 || actual_len > buf_len) {
        free(binary);
        return;
    }

    pthread_mutex_lock(&g_cache_mutex);
    store_mem_progbin_locked(&key, format, binary, (uint32_t)actual_len);
    pthread_mutex_unlock(&g_cache_mutex);

    if (!disk_ok) {
        free(binary);
        return;
    }

    char final_path[1152];
    snprintf(final_path, sizeof(final_path), "%s/pbin_%016llx_%016llx.ep1", dir_copy,
             (unsigned long long)key.h1, (unsigned long long)key.h2);
    if (access(final_path, F_OK) == 0) {
        free(binary);
        return;
    }

    char tmp_path[1184];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d.%lu", final_path, (int)getpid(), (unsigned long)pthread_self());
    FILE* fp = fopen(tmp_path, "wb");
    if (!fp) {
        free(binary);
        return;
    }

    uint32_t magic = EMESIS_PC_MAGIC;
    uint32_t fmt32 = (uint32_t)format;
    uint32_t len32 = (uint32_t)actual_len;
    int ok = write_exact(fp, &magic, sizeof(magic)) &&
             write_exact(fp, &key.h1, sizeof(key.h1)) &&
             write_exact(fp, &key.h2, sizeof(key.h2)) &&
             write_exact(fp, &fmt32, sizeof(fmt32)) &&
             write_exact(fp, &len32, sizeof(len32)) &&
             write_exact(fp, binary, (size_t)actual_len);
    fclose(fp);
    free(binary);

    if (ok) {
        if (rename(tmp_path, final_path) != 0) unlink(tmp_path);
    } else {
        unlink(tmp_path);
    }
}

void emesis_note_shader_compiler_activity(void) {
    ++g_compiler_dirty_count;
    g_quiet_clears_since_compile = 0;
}

static void emesis_do_reclaim_compiler_memory(void) {
    g_compiler_dirty_count = 0;
    g_quiet_clears_since_compile = 0;

    /* 1. Release native GLES driver's shader compiler AST/IR pools (e.g. libGLES_mali.so)
     * AND Emesis's glslang desktop-GLSL AST/symbol-table heap (~35-50 MB RSS). */
    extern void emesis_glslang_reclaim_pool(void);
    emesis_glslang_reclaim_pool();
    if (hardext.esversion > 1) {
        LOAD_GLES2(glReleaseShaderCompiler);
        if (gles_glReleaseShaderCompiler) {
            gles_glReleaseShaderCompiler();
        }
    }

    /* 2. Trim any program-binary RAM entries once backed by disk (keep g_mem_shaders hot so
     * mid-game FPE alpha-test shader specializations never stall on synchronous disk reads) */
    pthread_mutex_lock(&g_cache_mutex);
    if (g_disk_enabled) {
        for (size_t i = 0; i < EMESIS_MEM_PROGBIN_SLOTS; ++i) {
            if (g_mem_progbins[i].binary) {
                free(g_mem_progbins[i].binary);
                g_mem_progbins[i].binary = NULL;
            }
            g_mem_progbins[i].valid = 0;
            g_mem_progbins[i].length = 0;
        }
    }
    pthread_mutex_unlock(&g_cache_mutex);

    /* 3. Return freed allocator pages to the OS kernel after bulk shaderpack compile settles */
    {
#ifdef __ANDROID__
        typedef int (*mallopt_fn)(int, int);
        static mallopt_fn fn_mallopt = NULL;
        static int checked_mallopt = 0;
        if (!checked_mallopt) {
            checked_mallopt = 1;
            fn_mallopt = (mallopt_fn)dlsym(RTLD_DEFAULT, "mallopt");
        }
        if (fn_mallopt) {
            (void)fn_mallopt(-101 /* M_PURGE */, 0);
            (void)fn_mallopt(-104 /* M_PURGE_ALL */, 0);
        }
#endif
        typedef int (*malloc_trim_fn)(size_t);
        static malloc_trim_fn fn_mtrim = NULL;
        static int checked_mtrim = 0;
        if (!checked_mtrim) {
            checked_mtrim = 1;
            fn_mtrim = (malloc_trim_fn)dlsym(RTLD_DEFAULT, "malloc_trim");
        }
        if (fn_mtrim) {
            (void)fn_mtrim(0);
        }
    }
}

void emesis_force_reclaim_compiler_memory(void) {
    if (!g_compiler_dirty_count) return;
    emesis_do_reclaim_compiler_memory();
}

void emesis_maybe_reclaim_compiler_memory(void) {
    /* Run driver compiler teardown + M_PURGE after a bulk shaderpack compile/link batch
     * (>= 8 shaders/programs) followed by >= 24 quiet glClear calls (~0.4s at 60 FPS),
     * freeing Mali Bifrost compiler IR pools + Scudo/glibc arenas before gameplay/screenshots. */
    if (g_compiler_dirty_count < 8u) return;
    if (++g_quiet_clears_since_compile < 24u) return;
    emesis_do_reclaim_compiler_memory();
}
