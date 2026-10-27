#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "mc_version_detector.h"

#include <ctype.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* Cutoff: MC 1.16.5 is world_version 2586; MC 1.17 OpenGL 3.2 Core switch occurred in 21w10a (2698). */
#define EMESIS_LEGACY_MAX_WORLD_VERSION 2697

emesis_engine_t emesis_classify_world_version(int world_version) {
    if (world_version <= EMESIS_LEGACY_MAX_WORLD_VERSION) {
        return EMESIS_ENGINE_LEGACY;
    }
    return EMESIS_ENGINE_MODERN;
}

static bool starts_with_ci(const char* s, const char* prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
        s++;
        prefix++;
    }
    return true;
}

static bool contains_ci(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return false;
    size_t nlen = strlen(needle);
    for (const char* p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

int emesis_classify_mc_version_string(const char* raw) {
    if (!raw || !*raw) return -1;

    const char* s = raw;
    while (*s && (isspace((unsigned char)*s) || *s == '"' || *s == '\'')) s++;
    if (!*s) return -1;

    /* Classic / Pre-Classic / Indev / Infdev / Alpha / Beta -> <= 1.16.5 (Legacy) */
    if (starts_with_ci(s, "rd-") || starts_with_ci(s, "c0.") ||
        starts_with_ci(s, "in-") || starts_with_ci(s, "inf-") ||
        starts_with_ci(s, "a1.") || starts_with_ci(s, "b1.") ||
        starts_with_ci(s, "alpha") || starts_with_ci(s, "beta")) {
        return EMESIS_ENGINE_LEGACY;
    }

    /* NeoForge only exists for Minecraft 1.20.2+ and 26.x+ -> always Modern */
    if (contains_ci(s, "neoforge")) {
        return EMESIS_ENGINE_MODERN;
    }

    /* Strip common prefixes like "fabric-loader-0.16.9-1.21.1", "fabric-loader-0.18.4-26.3-pre-1", "quilt-loader-..." */
    if (starts_with_ci(s, "fabric-loader-")) {
        const char* after_loader = strchr(s + 14, '-');
        if (after_loader && after_loader[1]) s = after_loader + 1;
    } else if (starts_with_ci(s, "quilt-loader-")) {
        const char* after_loader = strchr(s + 13, '-');
        if (after_loader && after_loader[1]) s = after_loader + 1;
    } else if (starts_with_ci(s, "mc-") || starts_with_ci(s, "minecraft-")) {
        const char* dash = strchr(s, '-');
        if (dash && dash[1]) s = dash + 1;
    }

    /* Check weekly snapshots: YYwWWx (e.g., "20w51a", "21w08b", "21w10a", "24w14a") */
    if (isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
        (s[2] == 'w' || s[2] == 'W') &&
        isdigit((unsigned char)s[3]) && isdigit((unsigned char)s[4])) {
        int yy = (s[0] - '0') * 10 + (s[1] - '0');
        int ww = (s[3] - '0') * 10 + (s[4] - '0');
        if (yy < 21) return EMESIS_ENGINE_LEGACY;
        if (yy > 21) return EMESIS_ENGINE_MODERN;
        /* In 2021: 21w01a..21w08b used GL 2.1; 21w10a+ introduced OpenGL 3.2 Core profile! */
        return (ww >= 10) ? EMESIS_ENGINE_MODERN : EMESIS_ENGINE_LEGACY;
    }

    /* Parse leading major.minor(.patch) */
    if (isdigit((unsigned char)s[0])) {
        char* end1 = NULL;
        long major = strtol(s, &end1, 10);
        if (end1 && *end1 == '.' && isdigit((unsigned char)end1[1])) {
            char* end2 = NULL;
            long minor = strtol(end1 + 1, &end2, 10);
            long patch = 0;
            if (end2 && *end2 == '.' && isdigit((unsigned char)end2[1])) {
                patch = strtol(end2 + 1, NULL, 10);
            }

            if (major == 0) {
                return EMESIS_ENGINE_LEGACY;
            }
            if (major == 1) {
                /* 1.0 .. 1.16.5 => Legacy; 1.17+ => Modern */
                if (minor < 16) return EMESIS_ENGINE_LEGACY;
                if (minor == 16) {
                    /* 1.16 through 1.16.5 => Legacy */
                    return (patch <= 5) ? EMESIS_ENGINE_LEGACY : EMESIS_ENGINE_MODERN;
                }
                return EMESIS_ENGINE_MODERN;
            }
            /* Year-based Minecraft releases (e.g. 25.x, 26.1, 26.2, 26.3, 27.1+) => Modern */
            if (major >= 25 && major <= 99) {
                return EMESIS_ENGINE_MODERN;
            }
        }
    }

    /* Scan embedded substrings left-to-right for MC version tokens:
       - "mc26.3", "forge-26.3-1.0.2", "OptiFine_26.3_HD_U_J9" -> Modern
       - embedded snapshots like "fabric-25w04a" -> Modern/Legacy
       - "OptiFine_1.8.9", "1.12.2-forge-14.23.5" -> Legacy/Modern */
    for (const char* p = s; *p; p++) {
        bool boundary = (p == s || (!isalnum((unsigned char)p[-1]) && p[-1] != '.'));
        bool mc_prefixed = (p >= s + 2 &&
                            (p[-2] == 'm' || p[-2] == 'M') &&
                            (p[-1] == 'c' || p[-1] == 'C') &&
                            (p == s + 2 || !isalpha((unsigned char)p[-3])));

        if (boundary && isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) &&
            (p[2] == 'w' || p[2] == 'W') &&
            isdigit((unsigned char)p[3]) && isdigit((unsigned char)p[4]) &&
            isalpha((unsigned char)p[5])) {
            int yy = (p[0] - '0') * 10 + (p[1] - '0');
            int ww = (p[3] - '0') * 10 + (p[4] - '0');
            if (yy < 21) return EMESIS_ENGINE_LEGACY;
            if (yy > 21) return EMESIS_ENGINE_MODERN;
            return (ww >= 10) ? EMESIS_ENGINE_MODERN : EMESIS_ENGINE_LEGACY;
        }

        if ((boundary || mc_prefixed) && isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) &&
            p[2] == '.' && isdigit((unsigned char)p[3])) {
            long major = (p[0] - '0') * 10 + (p[1] - '0');
            if (major >= 25 && major <= 99) {
                char* end_min = NULL;
                long minor = strtol(p + 3, &end_min, 10);
                long patch = 0;
                bool has_patch = false;
                if (end_min && *end_min == '.' && isdigit((unsigned char)end_min[1])) {
                    has_patch = true;
                    patch = strtol(end_min + 1, &end_min, 10);
                }
                /* Distinguish MC year versions (e.g. 26.1, 26.3, 26.3.1) from 3-part Forge build numbers (e.g. 36.2.39) */
                if (minor >= 0 && minor <= 12 && (!has_patch || patch <= 12) &&
                    (!end_min || *end_min != '.')) {
                    return EMESIS_ENGINE_MODERN;
                }
            }
        }

        if ((boundary || mc_prefixed) && p[0] == '1' && p[1] == '.' && isdigit((unsigned char)p[2])) {
            char* end_min = NULL;
            long minor = strtol(p + 2, &end_min, 10);
            if (minor >= 0 && minor <= 30) {
                long patch = 0;
                if (end_min && *end_min == '.' && isdigit((unsigned char)end_min[1])) {
                    patch = strtol(end_min + 1, NULL, 10);
                }
                if (minor < 16) return EMESIS_ENGINE_LEGACY;
                if (minor == 16) return (patch <= 5) ? EMESIS_ENGINE_LEGACY : EMESIS_ENGINE_MODERN;
                return EMESIS_ENGINE_MODERN;
            }
        }
    }

    return -1;
}

static bool read_small_file(const char* path, char* buf, size_t cap) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n > 0;
}

static int parse_json_int_field(const char* json, const char* key) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p && (isspace((unsigned char)*p) || *p == ':')) p++;
    if (!isdigit((unsigned char)*p)) return -1;
    return (int)strtol(p, NULL, 10);
}

static bool parse_json_str_field(const char* json, const char* key, char* out, size_t out_cap) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return false;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_cap) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

/*
 * Inspects a Minecraft client .jar across all Pojav variants:
 * - Minecraft 1.14+ (18w47b+) ships `version.json` with `world_version` and `id`.
 * - Minecraft <= 1.13.2 (Classic, Beta, 1.0..1.13.2, including Cleanroom 1.12.2 on Java 21)
 *   contains `net/minecraft/client/Minecraft.class` or `com/mojang/minecraft/` WITHOUT `version.json`.
 */
static bool inspect_jar_version_json(const char* jar_path, emesis_route_decision_t* out) {
    if (!jar_path || !*jar_path) return false;
    FILE* f = fopen(jar_path, "rb");
    if (!f) return false;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long file_size = ftell(f);
    if (file_size < 22) {
        fclose(f);
        return false;
    }

    long search_len = file_size < 65557 ? file_size : 65557;
    uint8_t* tail = (uint8_t*)malloc((size_t)search_len);
    if (!tail) {
        fclose(f);
        return false;
    }
    if (fseek(f, file_size - search_len, SEEK_SET) != 0 ||
        fread(tail, 1, (size_t)search_len, f) != (size_t)search_len) {
        free(tail);
        fclose(f);
        return false;
    }

    long eocd_pos = -1;
    for (long i = search_len - 22; i >= 0; i--) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            eocd_pos = i;
            break;
        }
    }
    if (eocd_pos < 0) {
        free(tail);
        fclose(f);
        return false;
    }

    uint32_t cd_size = (uint32_t)tail[eocd_pos + 12] |
                       ((uint32_t)tail[eocd_pos + 13] << 8) |
                       ((uint32_t)tail[eocd_pos + 14] << 16) |
                       ((uint32_t)tail[eocd_pos + 15] << 24);
    uint32_t cd_offset = (uint32_t)tail[eocd_pos + 16] |
                         ((uint32_t)tail[eocd_pos + 17] << 8) |
                         ((uint32_t)tail[eocd_pos + 18] << 16) |
                         ((uint32_t)tail[eocd_pos + 19] << 24);
    free(tail);

    if (cd_size == 0 || cd_size > 16 * 1024 * 1024 || (long)cd_offset + (long)cd_size > file_size) {
        fclose(f);
        return false;
    }

    uint8_t* cd = (uint8_t*)malloc(cd_size);
    if (!cd) {
        fclose(f);
        return false;
    }
    if (fseek(f, (long)cd_offset, SEEK_SET) != 0 || fread(cd, 1, cd_size, f) != cd_size) {
        free(cd);
        fclose(f);
        return false;
    }

    bool found = false;
    bool has_version_json = false;
    bool is_mc_client_jar = false;
    size_t pos = 0;
    while (pos + 46 <= cd_size) {
        if (!(cd[pos] == 0x50 && cd[pos + 1] == 0x4b && cd[pos + 2] == 0x01 && cd[pos + 3] == 0x02)) {
            break;
        }
        uint16_t method = (uint16_t)cd[pos + 10] | ((uint16_t)cd[pos + 11] << 8);
        uint32_t comp_size = (uint32_t)cd[pos + 20] | ((uint32_t)cd[pos + 21] << 8) |
                             ((uint32_t)cd[pos + 22] << 16) | ((uint32_t)cd[pos + 23] << 24);
        uint32_t uncomp_size = (uint32_t)cd[pos + 24] | ((uint32_t)cd[pos + 25] << 8) |
                               ((uint32_t)cd[pos + 26] << 16) | ((uint32_t)cd[pos + 27] << 24);
        uint16_t name_len = (uint16_t)cd[pos + 28] | ((uint16_t)cd[pos + 29] << 8);
        uint16_t extra_len = (uint16_t)cd[pos + 30] | ((uint16_t)cd[pos + 31] << 8);
        uint16_t comment_len = (uint16_t)cd[pos + 32] | ((uint16_t)cd[pos + 33] << 8);
        uint32_t lfh_offset = (uint32_t)cd[pos + 42] | ((uint32_t)cd[pos + 43] << 8) |
                              ((uint32_t)cd[pos + 44] << 16) | ((uint32_t)cd[pos + 45] << 24);

        if (pos + 46 + name_len > cd_size) break;
        const char* entry_name = (const char*)(cd + pos + 46);

        if (!is_mc_client_jar) {
            if ((name_len == 36 && memcmp(entry_name, "net/minecraft/client/Minecraft.class", 36) == 0) ||
                (name_len == 36 && memcmp(entry_name, "net/minecraft/client/main/Main.class", 36) == 0) ||
                (name_len >= 21 && memcmp(entry_name, "com/mojang/minecraft/", 21) == 0)) {
                is_mc_client_jar = true;
            }
        }

        if (name_len == 12 && memcmp(entry_name, "version.json", 12) == 0 &&
            uncomp_size > 0 && uncomp_size < 65536 && comp_size < 65536) {
            has_version_json = true;
            uint8_t lfh[30];
            if (fseek(f, (long)lfh_offset, SEEK_SET) == 0 && fread(lfh, 1, 30, f) == 30 &&
                lfh[0] == 0x50 && lfh[1] == 0x4b && lfh[2] == 0x03 && lfh[3] == 0x04) {
                uint16_t l_name = (uint16_t)lfh[26] | ((uint16_t)lfh[27] << 8);
                uint16_t l_extra = (uint16_t)lfh[28] | ((uint16_t)lfh[29] << 8);
                long data_off = (long)lfh_offset + 30 + l_name + l_extra;
                uint8_t* comp_buf = (uint8_t*)malloc(comp_size + 1);
                char* json_buf = (char*)malloc(uncomp_size + 1);
                if (comp_buf && json_buf && fseek(f, data_off, SEEK_SET) == 0 &&
                    fread(comp_buf, 1, comp_size, f) == comp_size) {
                    bool ok = false;
                    if (method == 0 && comp_size == uncomp_size) {
                        memcpy(json_buf, comp_buf, uncomp_size);
                        json_buf[uncomp_size] = '\0';
                        ok = true;
                    } else if (method == 8) {
                        z_stream zs;
                        memset(&zs, 0, sizeof(zs));
                        zs.next_in = comp_buf;
                        zs.avail_in = comp_size;
                        zs.next_out = (Bytef*)json_buf;
                        zs.avail_out = uncomp_size;
                        if (inflateInit2(&zs, -MAX_WBITS) == Z_OK) {
                            int zret = inflate(&zs, Z_FINISH);
                            inflateEnd(&zs);
                            if (zret == Z_STREAM_END || zs.total_out > 0) {
                                json_buf[zs.total_out] = '\0';
                                ok = true;
                            }
                        }
                    }
                    if (ok) {
                        int wv = parse_json_int_field(json_buf, "world_version");
                        char id_str[64] = {0};
                        parse_json_str_field(json_buf, "id", id_str, sizeof(id_str));
                        if (wv > 0) {
                            out->engine = emesis_classify_world_version(wv);
                            out->world_version = wv;
                            snprintf(out->detected_version, sizeof(out->detected_version), "%s",
                                     id_str[0] ? id_str : "version.json");
                            snprintf(out->detection_source, sizeof(out->detection_source),
                                     "client.jar!/version.json (world_version=%d)", wv);
                            found = true;
                        } else if (id_str[0]) {
                            int cls = emesis_classify_mc_version_string(id_str);
                            if (cls >= 0) {
                                out->engine = (emesis_engine_t)cls;
                                snprintf(out->detected_version, sizeof(out->detected_version), "%s", id_str);
                                snprintf(out->detection_source, sizeof(out->detection_source),
                                         "client.jar!/version.json (id)");
                                found = true;
                            }
                        }
                    }
                }
                free(comp_buf);
                free(json_buf);
            }
            break;
        }
        pos += 46 + name_len + extra_len + comment_len;
    }

    if (!found && is_mc_client_jar && !has_version_json) {
        out->engine = EMESIS_ENGINE_LEGACY;
        out->world_version = 0;
        snprintf(out->detected_version, sizeof(out->detected_version), "<= 1.13.2");
        snprintf(out->detection_source, sizeof(out->detection_source),
                 "client.jar (pre-1.14 archive without version.json)");
        found = true;
    }

    free(cd);
    fclose(f);
    return found;
}

static bool scan_classpath_for_mc(const char* cp, emesis_route_decision_t* out) {
    if (!cp || !*cp) return false;
    char* copy = strdup(cp);
    if (!copy) return false;

    char* saveptr = NULL;
    for (char* tok = strtok_r(copy, ":;", &saveptr); tok != NULL; tok = strtok_r(NULL, ":;", &saveptr)) {
        if (strstr(tok, "/versions/") || strstr(tok, "minecraft") || strstr(tok, "client")) {
            if (inspect_jar_version_json(tok, out)) {
                free(copy);
                return true;
            }
            const char* ver_dir = strstr(tok, "/versions/");
            if (ver_dir) {
                ver_dir += strlen("/versions/");
                char seg[64] = {0};
                size_t i = 0;
                while (ver_dir[i] && ver_dir[i] != '/' && i + 1 < sizeof(seg)) {
                    seg[i] = ver_dir[i];
                    i++;
                }
                seg[i] = '\0';
                int cls = emesis_classify_mc_version_string(seg);
                if (cls >= 0) {
                    out->engine = (emesis_engine_t)cls;
                    snprintf(out->detected_version, sizeof(out->detected_version), "%s", seg);
                    snprintf(out->detection_source, sizeof(out->detection_source), "classpath /versions/%s", seg);
                    free(copy);
                    return true;
                }
            }
        }
    }
    free(copy);
    return false;
}

static bool detect_via_jvm(emesis_route_decision_t* out) {
    typedef jint (*JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);
    JNI_GetCreatedJavaVMs_t get_vms = (JNI_GetCreatedJavaVMs_t)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
    if (!get_vms) return false;

    JavaVM* vm = NULL;
    jsize count = 0;
    if (get_vms(&vm, 1, &count) != JNI_OK || count <= 0 || !vm) return false;

    JNIEnv* env = NULL;
    jint env_stat = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (env_stat != JNI_OK || !env) return false;

    jclass sys_cls = (*env)->FindClass(env, "java/lang/System");
    if (!sys_cls) {
        (*env)->ExceptionClear(env);
        return false;
    }
    jmethodID get_prop = (*env)->GetStaticMethodID(
        env, sys_cls, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
    if (!get_prop) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, sys_cls);
        return false;
    }

    const char* prop_keys[] = {
        "fml.mcVersion",
        "fabric.gameVersion",
        "quilt.gameVersion",
        "minecraft.version",
        "org.lwjgl.glfw.checkThread0",
        NULL
    };
    for (int i = 0; prop_keys[i] != NULL; i++) {
        jstring jkey = (*env)->NewStringUTF(env, prop_keys[i]);
        jstring jval = (jstring)(*env)->CallStaticObjectMethod(env, sys_cls, get_prop, jkey);
        (*env)->DeleteLocalRef(env, jkey);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            continue;
        }
        if (jval) {
            const char* cval = (*env)->GetStringUTFChars(env, jval, NULL);
            int cls = emesis_classify_mc_version_string(cval);
            if (cls >= 0) {
                out->engine = (emesis_engine_t)cls;
                snprintf(out->detected_version, sizeof(out->detected_version), "%s", cval);
                snprintf(out->detection_source, sizeof(out->detection_source), "JVM property %s", prop_keys[i]);
                (*env)->ReleaseStringUTFChars(env, jval, cval);
                (*env)->DeleteLocalRef(env, jval);
                (*env)->DeleteLocalRef(env, sys_cls);
                return true;
            }
            (*env)->ReleaseStringUTFChars(env, jval, cval);
            (*env)->DeleteLocalRef(env, jval);
        }
    }

    /* Check minecraft.client.jar property directly if set by Pojav/ModLoaders */
    {
        jstring jkey = (*env)->NewStringUTF(env, "minecraft.client.jar");
        jstring jjar = (jstring)(*env)->CallStaticObjectMethod(env, sys_cls, get_prop, jkey);
        (*env)->DeleteLocalRef(env, jkey);
        if (!(*env)->ExceptionCheck(env) && jjar) {
            const char* cjar = (*env)->GetStringUTFChars(env, jjar, NULL);
            bool ok = inspect_jar_version_json(cjar, out);
            (*env)->ReleaseStringUTFChars(env, jjar, cjar);
            (*env)->DeleteLocalRef(env, jjar);
            if (ok) {
                (*env)->DeleteLocalRef(env, sys_cls);
                return true;
            }
        } else {
            (*env)->ExceptionClear(env);
        }
    }

    jstring cp_key = (*env)->NewStringUTF(env, "java.class.path");
    jstring jcp = (jstring)(*env)->CallStaticObjectMethod(env, sys_cls, get_prop, cp_key);
    (*env)->DeleteLocalRef(env, cp_key);
    if (!(*env)->ExceptionCheck(env) && jcp) {
        const char* ccp = (*env)->GetStringUTFChars(env, jcp, NULL);
        bool ok = scan_classpath_for_mc(ccp, out);
        (*env)->ReleaseStringUTFChars(env, jcp, ccp);
        (*env)->DeleteLocalRef(env, jcp);
        if (ok) {
            (*env)->DeleteLocalRef(env, sys_cls);
            return true;
        }
    } else {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, sys_cls);
    return false;
}

static bool detect_via_proc_cmdline(emesis_route_decision_t* out) {
    size_t cap = 131072;
    char* buf = (char*)malloc(cap);
    if (!buf) return false;
    FILE* f = fopen("/proc/self/cmdline", "rb");
    if (!f) {
        free(buf);
        return false;
    }
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    if (n == 0) {
        free(buf);
        return false;
    }
    buf[n] = '\0';

    const char* prev = NULL;
    const char* game_dir = NULL;
    size_t i = 0;
    while (i < n) {
        const char* arg = buf + i;
        size_t len = strlen(arg);
        if (prev) {
            if (strcmp(prev, "--version") == 0 || strcmp(prev, "--fml.mcVersion") == 0) {
                int cls = emesis_classify_mc_version_string(arg);
                if (cls >= 0) {
                    out->engine = (emesis_engine_t)cls;
                    snprintf(out->detected_version, sizeof(out->detected_version), "%s", arg);
                    snprintf(out->detection_source, sizeof(out->detection_source), "/proc/self/cmdline (%s)", prev);
                    free(buf);
                    return true;
                }
            } else if (strcmp(prev, "-cp") == 0 || strcmp(prev, "-classpath") == 0) {
                if (scan_classpath_for_mc(arg, out)) {
                    free(buf);
                    return true;
                }
            } else if (strcmp(prev, "--gameDir") == 0) {
                game_dir = arg;
            }
        }
        if (strncmp(arg, "-Dminecraft.client.jar=", 23) == 0) {
            if (inspect_jar_version_json(arg + 23, out)) {
                free(buf);
                return true;
            }
        }
        if (strncmp(arg, "-Dfml.mcVersion=", 16) == 0 ||
            strncmp(arg, "-Dfabric.gameVersion=", 21) == 0 ||
            strncmp(arg, "-Dquilt.gameVersion=", 20) == 0) {
            const char* eq = strchr(arg, '=');
            if (eq && eq[1]) {
                int cls = emesis_classify_mc_version_string(eq + 1);
                if (cls >= 0) {
                    out->engine = (emesis_engine_t)cls;
                    snprintf(out->detected_version, sizeof(out->detected_version), "%s", eq + 1);
                    snprintf(out->detection_source, sizeof(out->detection_source), "/proc/self/cmdline (-D property)");
                    free(buf);
                    return true;
                }
            }
        }
        prev = arg;
        i += len + 1;
    }

    if (game_dir && *game_dir) {
        char opt_path[512];
        snprintf(opt_path, sizeof(opt_path), "%s/options.txt", game_dir);
        char opt_buf[4096];
        if (read_small_file(opt_path, opt_buf, sizeof(opt_buf))) {
            const char* vp = strstr(opt_buf, "version:");
            if (vp && (vp == opt_buf || vp[-1] == '\n' || vp[-1] == '\r')) {
                int wv = atoi(vp + 8);
                if (wv > 0) {
                    out->engine = emesis_classify_world_version(wv);
                    out->world_version = wv;
                    snprintf(out->detected_version, sizeof(out->detected_version), "DataVersion %d", wv);
                    snprintf(out->detection_source, sizeof(out->detection_source), "options.txt (version:%d)", wv);
                    free(buf);
                    return true;
                }
            }
        }
    }

    free(buf);
    return false;
}

void emesis_detect_runtime_route(emesis_route_decision_t* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->engine = EMESIS_ENGINE_MODERN;
    out->world_version = -1;
    snprintf(out->detected_version, sizeof(out->detected_version), "1.20+");
    snprintf(out->detection_source, sizeof(out->detection_source), "default (> 1.16.5 modern pipeline)");

    /* 1. Explicit environment variable override (supports V2 plugin SelectableEnv "auto"|"modern"|"legacy") */
    const char* force_env = getenv("EMESIS_FORCE_CORE");
    if (force_env && *force_env && !starts_with_ci(force_env, "auto")) {
        if (starts_with_ci(force_env, "legacy") || strcmp(force_env, "0") == 0) {
            out->engine = EMESIS_ENGINE_LEGACY;
            snprintf(out->detected_version, sizeof(out->detected_version), "forced <= 1.16.5");
            snprintf(out->detection_source, sizeof(out->detection_source), "EMESIS_FORCE_CORE=%s", force_env);
            return;
        }
        if (starts_with_ci(force_env, "modern") || strcmp(force_env, "1") == 0) {
            out->engine = EMESIS_ENGINE_MODERN;
            snprintf(out->detected_version, sizeof(out->detected_version), "forced > 1.16.5");
            snprintf(out->detection_source, sizeof(out->detection_source), "EMESIS_FORCE_CORE=%s", force_env);
            return;
        }
    }

    /* 2. Companion app config override (/sdcard/EmesisRenderer/config.json) */
    char cfg_buf[2048];
    if (read_small_file("/sdcard/EmesisRenderer/config.json", cfg_buf, sizeof(cfg_buf))) {
        char policy[64] = {0};
        if (parse_json_str_field(cfg_buf, "routingPolicy", policy, sizeof(policy))) {
            if (strcmp(policy, "force_legacy") == 0) {
                out->engine = EMESIS_ENGINE_LEGACY;
                snprintf(out->detected_version, sizeof(out->detected_version), "forced <= 1.16.5");
                snprintf(out->detection_source, sizeof(out->detection_source), "/sdcard/EmesisRenderer/config.json");
                return;
            }
            if (strcmp(policy, "force_modern") == 0) {
                out->engine = EMESIS_ENGINE_MODERN;
                snprintf(out->detected_version, sizeof(out->detected_version), "forced > 1.16.5");
                snprintf(out->detection_source, sizeof(out->detection_source), "/sdcard/EmesisRenderer/config.json");
                return;
            }
        }
    }

    /* 3. Pojav / Zalith / FCL / Boat version environment variables */
    const char* mc_env_keys[] = {
        "INST_MC_VER",
        "MC_VERSION",
        "GAME_VERSION",
        "POJAV_MC_VERSION",
        "ZALITH_MC_VERSION",
        "FCL_MC_VERSION",
        "MINECRAFT_VERSION",
        NULL
    };
    for (int i = 0; mc_env_keys[i] != NULL; i++) {
        const char* val = getenv(mc_env_keys[i]);
        if (val && *val) {
            int cls = emesis_classify_mc_version_string(val);
            if (cls >= 0) {
                out->engine = (emesis_engine_t)cls;
                snprintf(out->detected_version, sizeof(out->detected_version), "%s", val);
                snprintf(out->detection_source, sizeof(out->detection_source), "env %s", mc_env_keys[i]);
                return;
            }
        }
    }

    /* 4. Active OpenJDK JVM system properties & client.jar!/version.json */
    if (detect_via_jvm(out)) return;

    /* 5. /proc/self/cmdline arguments & gameDir options.txt */
    if (detect_via_proc_cmdline(out)) return;

    /* 6. JAVA_HOME/release fallback */
    const char* java_home = getenv("JAVA_HOME");
    if (java_home && *java_home) {
        char rel_path[512];
        snprintf(rel_path, sizeof(rel_path), "%s/release", java_home);
        char rel_buf[2048];
        if (read_small_file(rel_path, rel_buf, sizeof(rel_buf))) {
            const char* jv = strstr(rel_buf, "JAVA_VERSION=\"");
            if (jv) {
                jv += 14;
                int jmajor = (strncmp(jv, "1.", 2) == 0) ? atoi(jv + 2) : atoi(jv);
                if (jmajor > 0) {
                    out->engine = (jmajor >= 16) ? EMESIS_ENGINE_MODERN : EMESIS_ENGINE_LEGACY;
                    snprintf(out->detected_version, sizeof(out->detected_version), "Java %d", jmajor);
                    snprintf(out->detection_source, sizeof(out->detection_source), "JAVA_HOME/release");
                    return;
                }
            }
        }
    }
}
