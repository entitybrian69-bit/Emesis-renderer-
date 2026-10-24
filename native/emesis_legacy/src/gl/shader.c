#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "shader.h"

#include "../glx/hardext.h"
#include "debug.h"
#include "fpe_shader.h"
#include "init.h"
#include "gl4es.h"
#include "glstate.h"
#include "loader.h"
#include "shaderconv.h"
#include "shader_cache.h"
#include "shadermod_uniforms.h"
#include "vgpu/shaderconv.h"
#include "glsl/glsl_for_es.h"
#include <ctype.h>
#include <string.h>

// #define DEBUG
#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

KHASH_MAP_IMPL_INT(shaderlist, shader_t*);

GLuint APIENTRY_GL4ES gl4es_glCreateShader(GLenum shaderType) {
    DBG(SHUT_LOGD("glCreateShader(%s)\n", PrintEnum(shaderType)))
    // sanity check
    if (shaderType != GL_VERTEX_SHADER && shaderType != GL_FRAGMENT_SHADER && shaderType != GL_GEOMETRY_SHADER &&
        shaderType != GL_COMPUTE_SHADER) {
        DBG(SHUT_LOGD("Invalid shader type\n"))
        errorShim(GL_INVALID_ENUM);
        return 0;
    }
    static GLuint lastshader = 0;
    GLuint shader;
    // create the shader
    LOAD_GLES2(glCreateShader);
    if (gles_glCreateShader) {
        shader = gles_glCreateShader(shaderType);
        if (!shader) {
            DBG(SHUT_LOGD("Failed to create shader\n"))
            errorGL();
            return 0;
        }
    } else {
        shader = ++lastshader;
        noerrorShim();
    }
    // store the new empty shader in the list for later use
    khint_t k;
    int ret;
    khash_t(shaderlist)* shaders = glstate->glsl->shaders;
    k = kh_get(shaderlist, shaders, shader);
    shader_t* glshader = NULL;
    if (k == kh_end(shaders)) {
        k = kh_put(shaderlist, shaders, shader, &ret);
        glshader = kh_value(shaders, k) = (shader_t*)calloc(1, sizeof(shader_t));
    } else {
        glshader = kh_value(shaders, k);
    }
    glshader->id = shader;
    glshader->type = shaderType;
    glshader->deleted = 0;
    glshader->attached = 0;
    glshader->compiled = 0;
    glshader->gles_compiled = 0;
    if (glshader->source) {
        free(glshader->source);
        glshader->source = NULL;
    }
    if (glshader->converted) {
        free(glshader->converted);
        glshader->converted = NULL;
    }
    if (glshader->before_patch) {
        free(glshader->before_patch);
        glshader->before_patch = NULL;
    }
    uniform_decls_clear(&glshader->uniforms_declarations);
    memset(&glshader->need, 0, sizeof(glshader->need));
    glshader->need.need_texcoord = -1;

    // all done
    return shader;
}

void actually_deleteshader(GLuint shader) {
    khint_t k;
    khash_t(shaderlist)* shaders = glstate->glsl->shaders;
    k = kh_get(shaderlist, shaders, shader);
    if (k != kh_end(shaders)) {
        shader_t* glshader = kh_value(shaders, k);
        if (glshader->deleted && !glshader->attached) {
            GLuint hw_id = glshader->id;
            kh_del(shaderlist, shaders, k);
            if (glshader->source) free(glshader->source);
            if (glshader->converted) free(glshader->converted);
            if (glshader->before_patch) free(glshader->before_patch);
            uniform_decls_clear(&glshader->uniforms_declarations);
            free(glshader);
            LOAD_GLES2(glDeleteShader);
            if (gles_glDeleteShader && hw_id) {
                gles_glDeleteShader(hw_id);
            }
        }
    }
}

void actually_detachshader(GLuint shader) {
    khint_t k;
    khash_t(shaderlist)* shaders = glstate->glsl->shaders;
    k = kh_get(shaderlist, shaders, shader);
    if (k != kh_end(shaders)) {
        shader_t* glshader = kh_value(shaders, k);
        if ((--glshader->attached) < 1 && glshader->deleted) actually_deleteshader(shader);
    }
}

void APIENTRY_GL4ES gl4es_glDeleteShader(GLuint shader) {
    DBG(SHUT_LOGD("glDeleteShader(%d)\n", shader))
    // sanity check...
    CHECK_SHADER(void, shader)
    // delete the shader from the list
    if (!glshader) {
        noerrorShim();
        return;
    }
    glshader->deleted = 1;
    noerrorShim();
    if (!glshader->attached) {
        actually_deleteshader(shader);
    }
}

bool is_direct_shader(char* glsl);

void ensure_shader_gles_compiled(shader_t* glshader) {
    if (!glshader || glshader->gles_compiled) return;
    LOAD_GLES2(glShaderSource);
    LOAD_GLES2(glCompileShader);
    if (gles_glShaderSource && gles_glCompileShader) {
        const GLchar* finalSource = glshader->converted ? glshader->converted : glshader->source;
        if (finalSource) {
            gles_glShaderSource(glshader->id, 1, &finalSource, NULL);
        }
        gles_glCompileShader(glshader->id);
        glshader->gles_compiled = 1;
        emesis_note_shader_compiler_activity();
        errorGL();
        {
            LOAD_GLES2(glGetShaderiv);
            LOAD_GLES2(glGetShaderInfoLog);
            GLint status = 0;
            gles_glGetShaderiv(glshader->id, GL_COMPILE_STATUS, &status);
            if (status != GL_TRUE) {
                DBG(SHUT_LOGD("LIBGL: Error while compiling shader %d. Original source is:\n%s\n=======\n",
                              glshader->id, glshader->source);)
                DBG(SHUT_LOGD("ShaderConv Source is:\n%s\n=======\n", glshader->converted);)
                char tmp[500];
                GLint length;
                gles_glGetShaderInfoLog(glshader->id, 500, &length, tmp);
                DBG(SHUT_LOGD("Compiler message is\n%s\nLIBGL: End of Error log\n", tmp);)
            }
        }
    } else {
        noerrorShim();
    }
}

void APIENTRY_GL4ES gl4es_glCompileShader(GLuint shader) {
    DBG(SHUT_LOGD("glCompileShader(%d)\n", shader))
    // look for the shader
    CHECK_SHADER(void, shader)

    glshader->compiled = 1;
    /* Defer GLES driver AST/IR compilation for all valid shaders until glLinkProgram
       misses the .ep1 program binary cache; compile failed-translation shaders immediately for error logs */
    if (!glshader->converted) {
        ensure_shader_gles_compiled(glshader);
    } else {
        noerrorShim();
    }
}

void set_es_version();

bool can_run_essl3(int esversion, const char* glsl) {
    if (!glsl) return false;
    while (*glsl == ' ' || *glsl == '\t' || *glsl == '\r' || *glsl == '\n') ++glsl;
    int glsl_version = 0;
    if (strncmp(glsl, "#version 100", 12) == 0) {
        return true;
    } else if (strncmp(glsl, "#version 300 es", 15) == 0) {
        return true;
    } else if (strncmp(glsl, "#version 310 es", 15) == 0) {
        glsl_version = 310;
    } else if (strncmp(glsl, "#version 320 es", 15) == 0) {
        glsl_version = 320;
    } else {
        return false;
    }
    return (esversion >= glsl_version);
}

bool is_direct_shader(char* glsl) {
    if (!glsl) return false;
    const char* s = glsl;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    bool es2_ability = glstate->glsl->es2 && !strncmp(s, "#version 100", 12);
    if (globals4es.es >= 3 && globals4es.esversion <= 0) set_es_version();
    int esv = (globals4es.esversion > 0) ? globals4es.esversion : (globals4es.es * 100);
    bool es3_ability = globals4es.es >= 3 && can_run_essl3(esv, s);
    return es2_ability || es3_ability;
}

char* replace_version_line(const char* text) {
    if (!text) return NULL;

    const char* line_start = text;
    const char* p = text;
    const char* replace_str = "#version 330 compatibility\n";
    size_t replace_len = strlen(replace_str);

    while (*p) {
        if (*p == '\n' || *(p + 1) == '\0') {
            size_t len = p - line_start + (*(p + 1) == '\0' ? 1 : 0);

            size_t i = 0;
            while (i < len && isspace((unsigned char)line_start[i]))
                i++;

            if (strncmp(line_start + i, "#version", 8) == 0) {
                size_t new_len = strlen(text) - len + replace_len;
                char* new_text = malloc(new_len + 1);
                if (!new_text) return NULL;

                memcpy(new_text, text, line_start - text);
                memcpy(new_text + (line_start - text), replace_str, replace_len);
                strcpy(new_text + (line_start - text) + replace_len, p + (*(p + 1) == '\0' ? 0 : 1));

                return new_text;
            }

            line_start = p + 1;
        }
        p++;
    }

    return strdup(text);
}

char* replace_version_line_460(char* text) {
    const char* new_version = "#version 460\n";
    if (!text || !new_version) return NULL;

    const char* p = text;
    const char* line_start = text;
    size_t text_len = strlen(text);
    size_t new_version_len = strlen(new_version);

    while (*p) {
        const char* line_end = strchr(p, '\n');
        const char* next_line = NULL;
        if (line_end) {
            next_line = line_end + 1;
        } else {
            line_end = text + text_len;
            next_line = line_end;
        }

        const char* cur = p;
        while (cur < line_end && isspace((unsigned char)*cur) && *cur != '\n' && *cur != '\r')
            cur++;

        const char* tok = "#version";
        size_t toklen = strlen(tok);
        if ((size_t)(line_end - cur) >= toklen && strncmp(cur, tok, toklen) == 0) {
            const char next_char = (cur + toklen < line_end) ? *(cur + toklen) : '\0';
            if (next_char == '\0' || isspace((unsigned char)next_char)) {
                size_t prefix_len = cur - text;
                prefix_len = p - text;

                const char* suffix_start = next_line;

                size_t suffix_len = strlen(suffix_start);
                size_t new_total = prefix_len + new_version_len + suffix_len;
                char* out = (char*)malloc(new_total + 1);
                if (!out) return NULL;

                memcpy(out, text, prefix_len);
                memcpy(out + prefix_len, new_version, new_version_len);
                memcpy(out + prefix_len + new_version_len, suffix_start, suffix_len);
                out[new_total] = '\0';
                return out;
            }
        }
        p = next_line;
    }

    return strdup(text);
}

bool check_version_compatibility(const char* str) {
    if (str == NULL) {
        return false;
    }

    const char* line_start = str;

    while (*line_start) {
        const char* line_end = strpbrk(line_start, "\n");
        size_t line_len;

        if (line_end) {
            line_len = line_end - line_start;
        } else {
            line_len = strlen(line_start);
        }

        if (strncmp(line_start, "#version", 8) == 0) {
            char current_line[line_len + 1];
            strncpy(current_line, line_start, line_len);
            current_line[line_len] = '\0';

            if (strstr(current_line, "compatibility") != NULL) {
                return true;
            }
        }

        if (line_end) {
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    return false;
}

void remove_before_version(char* str) {
    if (str == NULL) {
        return;
    }

    char* version_ptr = strstr(str, "#version");

    if (version_ptr != NULL) {
        size_t remaining_len = strlen(version_ptr) + 1;

        memmove(str, version_ptr, remaining_len);
    }
}

int has_gl_FragColor_definition(const char* s) {
    const char* p = s;
    while (*p) {
        if (strncmp(p, "vec4", 4) == 0) {
            p += 4;
            while (*p && isspace((unsigned char)*p))
                p++;
            if (strncmp(p, "gl4es_FragColor", 12) == 0) {
                return 1;
            }
        } else {
            p++;
        }
    }
    return 0;
}

char* replace_glFragColor(const char* src) {
    if (!src) return NULL;

    const char* target = "gl_FragColor";
    const char* replacement = "gl4es_FragColor";
    size_t target_len = strlen(target);
    size_t replacement_len = strlen(replacement);

    size_t count = 0;
    const char* p = src;
    while ((p = strstr(p, target)) != NULL) {
        if ((p == src || !((p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= 'A' && p[-1] <= 'Z') ||
                           (p[-1] >= '0' && p[-1] <= '9') || p[-1] == '_')) &&
            !((p[target_len] >= 'a' && p[target_len] <= 'z') || (p[target_len] >= 'A' && p[target_len] <= 'Z') ||
              (p[target_len] >= '0' && p[target_len] <= '9') || p[target_len] == '_')) {
            count++;
        }
        p += target_len;
    }

    size_t new_len = strlen(src) + count * (replacement_len - target_len) + 1;
    char* result = (char*)malloc(new_len);
    if (!result) return NULL;

    const char* src_p = src;
    char* dst_p = result;
    while ((p = strstr(src_p, target)) != NULL) {
        int valid =
            (p == src_p || !((p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= 'A' && p[-1] <= 'Z') ||
                             (p[-1] >= '0' && p[-1] <= '9') || p[-1] == '_')) &&
            !((p[target_len] >= 'a' && p[target_len] <= 'z') || (p[target_len] >= 'A' && p[target_len] <= 'Z') ||
              (p[target_len] >= '0' && p[target_len] <= '9') || p[target_len] == '_');
        size_t copy_len = p - src_p;
        memcpy(dst_p, src_p, copy_len);
        dst_p += copy_len;

        if (valid) {
            memcpy(dst_p, replacement, replacement_len);
            dst_p += replacement_len;
        } else {
            memcpy(dst_p, target, target_len);
            dst_p += target_len;
        }
        src_p = p + target_len;
    }
    strcpy(dst_p, src_p);

    return result;
}

char* insert_gl_FragColor_if_missing(const char* shader) {
    if (has_gl_FragColor_definition(shader)) {
        return strdup(shader);
    }

    const char* insert_line = "out vec4 gl4es_FragColor;\n";

    const char* pos = shader;
    const char* insert_pos = shader;
    if (strncmp(shader, "#version", 8) == 0) {
        const char* newline = strchr(shader, '\n');
        if (newline) {
            insert_pos = newline + 1;
        }
    }

    size_t new_len = strlen(shader) + strlen(insert_line) + 1;
    char* new_shader = (char*)malloc(new_len);
    if (!new_shader) return NULL;

    size_t head_len = insert_pos - shader;
    memcpy(new_shader, shader, head_len);
    strcpy(new_shader + head_len, insert_line);
    strcpy(new_shader + head_len + strlen(insert_line), insert_pos);

    return new_shader;
}

int contains_glFragColor(const char* src) {
    if (!src) return 0;

    const char* target = "gl_FragColor";
    size_t target_len = strlen(target);

    const char* p = src;
    while ((p = strstr(p, target)) != NULL) {
        int valid = 1;

        if (p != src && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) {
            valid = 0;
        }
        if (p[target_len] != '\0' && (isalnum((unsigned char)p[target_len]) || p[target_len] == '_')) {
            valid = 0;
        }

        if (valid) return 1;

        p += target_len;
    }

    return 0;
}

char* replace_fragdata0(const char* source) {
    const char* target = "gl_FragData[0].";
    const char* replacement = "vgpu_FragData0";
    size_t target_len = strlen(target);
    size_t replacement_len = strlen(replacement);

    size_t count = 0;
    const char* p = source;
    while ((p = strstr(p, target)) != NULL) {
        count++;
        p += target_len;
    }

    size_t new_len = strlen(source) + count * (replacement_len - target_len);
    char* result = (char*)malloc(new_len + 1);
    if (!result) return NULL;

    const char* src = source;
    char* dst = result;
    while ((p = strstr(src, target)) != NULL) {
        size_t len = p - src;
        memcpy(dst, src, len);
        dst += len;
        memcpy(dst, replacement, replacement_len);
        dst += replacement_len;
        src = p + target_len;
    }
    strcpy(dst, src);

    return result;
}

#define GLSL_CONVERTED_MARK "//__glsl_builtin_converted__\n"

char* mark_glsl_builtin_converted(const char* source) {
    if (!source) return NULL;
    size_t srcLen = strlen(source);
    size_t markLen = strlen(GLSL_CONVERTED_MARK);

    char* result = (char*)malloc(srcLen + markLen + 2);
    if (!result) return NULL;

    memcpy(result, source, srcLen);
    result[srcLen] = '\n';
    memcpy(result + srcLen + 1, GLSL_CONVERTED_MARK, markLen);
    result[srcLen + 1 + markLen] = '\0';

    return result;
}

int is_glsl_builtin_converted(const char* source) {
    if (!source) return 0;
    size_t srcLen = strlen(source);
    size_t markLen = strlen(GLSL_CONVERTED_MARK);

    if (srcLen < markLen) return 0;

    const char* tail = source + srcLen - markLen;
    return (strcmp(tail, GLSL_CONVERTED_MARK) == 0);
}

int first_three_lines_contains_BSL(const char* text) {
    if (!text) return 0;

    int line_count = 0;
    const char* line_start = text;
    const char* p = text;

    while (*p && line_count < 3) {
        if (*p == '\n' || *(p + 1) == '\0') {
            size_t len = p - line_start + (*(p + 1) == '\0' ? 1 : 0);

            if (len > 0) {
                if (memmem(line_start, len, "BSL", 3) != NULL) {
                    return 1;
                }
            }

            line_count++;
            line_start = p + 1;
        }
        p++;
    }
    return 0;
}

static char* replace_all(const char* src, const char* old, const char* repl) {
    if (!src || !old || !repl) return NULL;
    size_t src_len = strlen(src);
    size_t old_len = strlen(old);
    if (old_len == 0) return NULL;
    size_t repl_len = strlen(repl);

    size_t count = 0;
    const char* p = src;
    while ((p = strstr(p, old)) != NULL) {
        count++;
        p += old_len;
    }
    if (count == 0) {
        char* out = (char*)malloc(src_len + 1);
        if (out) memcpy(out, src, src_len + 1);
        return out;
    }

    size_t new_len = src_len + count * (repl_len - old_len);
    char* out = (char*)malloc(new_len + 1);
    if (!out) return NULL;

    const char* cur = src;
    char* dst = out;
    while ((p = strstr(cur, old)) != NULL) {
        size_t chunk = p - cur;
        memcpy(dst, cur, chunk);
        dst += chunk;
        memcpy(dst, repl, repl_len);
        dst += repl_len;
        cur = p + old_len;
    }
    size_t tail = strlen(cur);
    memcpy(dst, cur, tail);
    dst += tail;
    *dst = '\0';
    return out;
}

static char* insert_after_first(const char* src, const char* anchor, const char* insert_str) {
    if (!src || !anchor || !insert_str) return NULL;
    const char* p = strstr(src, anchor);
    if (!p) {
        char* out = (char*)malloc(strlen(src) + 1);
        if (out) memcpy(out, src, strlen(src) + 1);
        return out;
    }
    size_t prefix_len = (p - src) + strlen(anchor);
    size_t insert_len = strlen(insert_str);
    size_t src_len = strlen(src);
    size_t new_len = src_len + insert_len;
    char* out = (char*)malloc(new_len + 1);
    if (!out) return NULL;

    memcpy(out, src, prefix_len);
    memcpy(out + prefix_len, insert_str, insert_len);
    memcpy(out + prefix_len + insert_len, src + prefix_len, src_len - prefix_len);
    out[new_len] = '\0';
    return out;
}

char* bsl_patch(const char* glsl) {
    if (!glsl) return NULL;

    const char* old1 = "cloudBlendOpacity = step(viewLength, cloudViewLength);";
    const char* new1 = "cloudBlendOpacity = 0.7;";
    const char* old2 = "vgpu_FragData1 = vec4(temporalData, temporalColor);";
    const char* new2 = "vgpu_FragData1 = vec4(color, 1.0);";
    const char* old3 = "vgpu_FragData1 = vec4(vl, 1.0);";
    const char* new3 = "vgpu_FragData1 = color;";
    const char* old4 = "highp vec3 bloom = texture(colortex1, ((coord / vec2(scale)) + offset) * resScale).xyz;";
    const char* new4 = "highp vec3 bloom = vec3(0.0, 0.0, 0.0);";
    const char* old5 = "highp float isGlowing = texture(colortex3, texCoord).z;";
    const char* new5 = "highp float isGlowing = 0.0;";
    char* s = replace_all(glsl, old1, new1);
    if (!s) return NULL;
    char* s2 = replace_all(s, old2, new2);
    free(s);
    if (!s2) return NULL;
    char* s3 = replace_all(s2, old3, new3);
    free(s2);
    if (!s3) return NULL;
    char* s4 = replace_all(s3, old4, new4);
    free(s3);
    if (!s4) return NULL;
    char* s5 = replace_all(s4, old5, new5);
    free(s4);
    if (!s5) return NULL;

    const char* trigger = "highp float quality[";
    char* cur = s5;

    if (strstr(cur, trigger) != NULL) {
        const char* anchor0 = "layout(location = 0) out highp vec4 vgpu_FragData0;";
        const char* line0 = "\nlayout(location = 1) out highp vec4 vgpu_FragData1;";
        char* t = insert_after_first(cur, anchor0, line0);
        free(cur);
        cur = t ? t : strdup("");
        const char* anchor1 = "vgpu_FragData0 = vec4(color, 1.0);";
        const char* line1 = "\nvgpu_FragData1 = vec4(color,1.0);";
        t = insert_after_first(cur, anchor1, line1);
        free(cur);
        cur = t ? t : strdup("");
    }
    return cur;
}

static char* replace_version_to_es(const char* text, int esversion) {
    if (!text) return NULL;
    char new_version[50];
    snprintf(new_version, 50, "#version %d es\n", esversion);
    const char* p = text;
    const char* line_start = text;
    size_t text_len = strlen(text);
    while (*p) {
        const char* line_end = strchr(p, '\n');
        const char* next_line = NULL;
        if (line_end) {
            next_line = line_end + 1;
        } else {
            line_end = text + text_len;
            next_line = line_end;
        }
        const char* cur = p;
        while (cur < line_end && isspace((unsigned char)*cur) && *cur != '\n' && *cur != '\r')
            cur++;
        if (strncmp(cur, "#version", 8) == 0) {
            size_t prefix_len = p - text;
            size_t line_len = next_line - p;
            size_t suffix_len = text_len - (next_line - text);
            size_t new_total = prefix_len + strlen(new_version) + suffix_len;
            char* out = (char*)malloc(new_total + 1);
            if (!out) return NULL;
            memcpy(out, text, prefix_len);
            memcpy(out + prefix_len, new_version, strlen(new_version));
            memcpy(out + prefix_len + strlen(new_version), next_line, suffix_len);
            out[new_total] = '\0';
            return out;
        }
        p = next_line;
    }
    return strdup(text);
}

void set_es_version();
static bool uses_legacy_compat_builtins(const char* src) {
    if (!src) return false;
    return strstr(src, "gl_Vertex") != NULL ||
           strstr(src, "gl_Normal") != NULL ||
           strstr(src, "gl_Color") != NULL ||
           strstr(src, "gl_FrontColor") != NULL ||
           strstr(src, "gl_BackColor") != NULL ||
           strstr(src, "gl_SecondaryColor") != NULL ||
           strstr(src, "gl_MultiTexCoord") != NULL ||
           strstr(src, "gl_TexCoord") != NULL ||
           strstr(src, "gl_Fog") != NULL ||
           strstr(src, "gl_ModelView") != NULL ||
           strstr(src, "gl_Projection") != NULL ||
           strstr(src, "gl_TextureMatrix") != NULL ||
           strstr(src, "ftransform") != NULL ||
           strstr(src, "gl_FragData") != NULL ||
           strstr(src, "gl_FragColor") != NULL;
}

static bool has_explicit_core_profile(const char* str) {
    if (!str) return false;
    const char* p = str;
    while (*p) {
        const char* line_end = strchr(p, '\n');
        size_t len = line_end ? (size_t)(line_end - p) : strlen(p);
        const char* cur = p;
        while ((size_t)(cur - p) < len && (*cur == ' ' || *cur == '\t')) cur++;
        if ((size_t)(p + len - cur) >= 8 && strncmp(cur, "#version", 8) == 0) {
            for (const char* q = cur + 8; q + 4 <= p + len; ++q) {
                if (strncmp(q, "core", 4) == 0 &&
                    !isalnum((unsigned char)q[-1]) &&
                    (q + 4 == p + len || !isalnum((unsigned char)q[4]))) {
                    return true;
                }
            }
            return false;
        }
        if (!line_end) break;
        p = line_end + 1;
    }
    return false;
}

static int effective_target_es(void) {
    int esv = (globals4es.esversion > 0) ? globals4es.esversion : (globals4es.es * 100);
    if (hardext.glsl320es && esv >= 320) return 320;
    if (hardext.glsl310es && esv >= 310) return 310;
    if (hardext.glsl300es || esv >= 300) return 300;
    return 100;
}

static void translate_shader_with_cache(shader_t* glshader) {
    const char* base_src = glshader->source;
    if (!base_src) return;

    int target_es = effective_target_es();
    shaderconv_need_t req_need = glshader->need;

    if (glshader->converted) {
        free(glshader->converted);
        glshader->converted = NULL;
    }
    uniform_decls_clear(&glshader->uniforms_declarations);

    /* Fast path 1: RAM / Disk 128-bit shader translation cache */
    emesis_hash128_t cache_key = emesis_shader_compute_key(glshader->type, target_es, &req_need, base_src);
    char* cached_essl = NULL;
    shaderconv_need_t cached_need = req_need;
    int cached_is_essl = 0;
    if (emesis_shader_cache_lookup(&cache_key, &cached_essl, &cached_is_essl,
                                   &cached_need, &glshader->uniforms_declarations)) {
        glshader->converted = cached_essl;
        glshader->need = cached_need;
        glshader->is_converted_essl_320 = cached_is_essl;
        return;
    }

    int isFPEShader = (strstr(base_src, fpeshader_signature) != NULL) ? 1 : 0;
    if (isFPEShader || target_es < 300) {
        char* prev_src = glshader->source;
        glshader->source = (char*)base_src;
        char* cond = ConvertShaderConditionally(glshader);
        glshader->source = prev_src;
        glshader->converted = cond ? strdup(cond) : strdup(base_src);
        if (cond && cond != glshader->converted) {
            free(cond);
        }
        glshader->is_converted_essl_320 = 0;
        emesis_shader_cache_store(&cache_key, glshader->converted, 0,
                                  &glshader->need, &glshader->uniforms_declarations);
        return;
    }

    int isBSL = first_three_lines_contains_BSL(base_src);

    /* Strip uniform default initializers once up-front and capture default values cleanly */
    char* stripped = process_uniform_declarations((char*)base_src, &glshader->uniforms_declarations);
    if (!stripped) stripped = strdup(base_src);
    remove_before_version(stripped);

    int glsl_version = getGLSLVersion(stripped);
    bool need_compat = (glsl_version < 150) ||
                       check_version_compatibility(stripped) ||
                       !has_explicit_core_profile(stripped) ||
                       uses_legacy_compat_builtins(stripped);

    char* work = stripped;
    if (need_compat) {
        char* builtin_conv = ConvertShaderBuiltInVariableOnly(
            work, glshader->type == GL_VERTEX_SHADER ? 1 : 0, &glshader->need, 1);
        free(work);
        work = builtin_conv;

        if (glshader->type == GL_FRAGMENT_SHADER) {
            int sourceLength = (int)strlen(work) + 1;
            if (contains_glFragColor(work)) {
                work = ReplaceGLFragColor(work, &sourceLength);
            } else if (strstr(work, "gl_FragData")) {
                work = ReplaceGLFragData(work, &sourceLength);
            }
        }
    }

    int returnCode = -1;
    emesis_note_shader_compiler_activity();
    char* result = GLSLtoGLSLES_c(work, glshader->type, (unsigned int)target_es,
                                  (unsigned int)(glsl_version > 0 ? glsl_version : 120), &returnCode);
    free(work);

    if (result != NULL && returnCode >= 0) {
        if (isBSL || strstr(result, "cloudBlendOpacity = step(viewLength, cloudViewLength);") != NULL) {
            char* patched = bsl_patch(result);
            if (patched) {
                free(result);
                result = patched;
            }
        }
        glshader->converted = result;
        glshader->is_converted_essl_320 = 0; /* already stamped with exact `#version <target_es> es` */
    } else {
        free(result);
        /* Seamless ESSL 3.x fallback for any non-standard legacy GLSL 110/120 shader */
        uniform_decls_clear(&glshader->uniforms_declarations);
        glshader->need = req_need;
        char* prev_src = glshader->source;
        glshader->source = strdup(base_src);
        char* cond = ConvertShaderConditionally(glshader);
        char* final_cond = cond ? strdup(cond) : strdup(base_src);
        if (cond) free(cond);
        free(glshader->source);
        glshader->source = prev_src;
        glshader->converted = final_cond;
        glshader->is_converted_essl_320 = 0;
    }

    emesis_shader_cache_store(&cache_key, glshader->converted, glshader->is_converted_essl_320,
                              &glshader->need, &glshader->uniforms_declarations);
}

void APIENTRY_GL4ES gl4es_glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string,
                                         const GLint* length) {
    if (!globals4es.esversion) set_es_version();
    DBG(SHUT_LOGD("glShaderSource(%d, %d, %p, %p)\n", shader, count, string, length))
    if (count <= 0 || !string) {
        errorShim(GL_INVALID_VALUE);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (!string[i]) {
            errorShim(GL_INVALID_VALUE);
            return;
        }
    }

    CHECK_SHADER(void, shader)

    /* O(N) single-pass length measurement + memcpy concatenation (replaces O(N^2) strcat) */
    size_t total_len = 0;
    size_t stack_lens[64];
    size_t* part_lens = (count <= 64) ? stack_lens : (size_t*)malloc((size_t)count * sizeof(size_t));
    if (!part_lens) {
        errorShim(GL_OUT_OF_MEMORY);
        return;
    }
    for (int i = 0; i < count; i++) {
        size_t slen = (length && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
        part_lens[i] = slen;
        total_len += slen;
    }

    if (glshader->source) free(glshader->source);
    if (glshader->converted) {
        free(glshader->converted);
        glshader->converted = NULL;
    }
    if (glshader->before_patch) {
        free(glshader->before_patch);
        glshader->before_patch = NULL;
    }
    glshader->compiled = 0;
    glshader->gles_compiled = 0;
    uniform_decls_clear(&glshader->uniforms_declarations);
    memset(&glshader->need, 0, sizeof(glshader->need));
    glshader->need.need_texcoord = -1;

    glshader->source = (char*)malloc(total_len + 1);
    if (!glshader->source) {
        if (part_lens != stack_lens) free(part_lens);
        errorShim(GL_OUT_OF_MEMORY);
        return;
    }
    size_t offset = 0;
    for (int i = 0; i < count; i++) {
        if (part_lens[i] > 0) {
            memcpy(glshader->source + offset, string[i], part_lens[i]);
            offset += part_lens[i];
        }
    }
    glshader->source[offset] = '\0';
    if (part_lens != stack_lens) free(part_lens);

    emesis_sm_scan_shader_defines(glshader->source);

    LOAD_GLES2(glShaderSource);
    if (gles_glShaderSource) {
        int direct = is_direct_shader(glshader->source) ? 1 : 0;
        if (direct) {
            glshader->converted = strdup(glshader->source);
        } else {
            translate_shader_with_cache(glshader);
        }

        if (!glshader->converted) {
            GLchar* finalSource = glshader->source;
            const GLchar* sources[] = {finalSource};
            gles_glShaderSource(glshader->id, 1, sources, NULL);
            errorGL();
        } else {
            noerrorShim();
        }
    } else {
        noerrorShim();
    }
}

#define SUPER()                                                                                                        \
    GO(color)                                                                                                          \
    GO(secondary)                                                                                                      \
    GO(fogcoord)                                                                                                       \
    GO(texcoord)                                                                                                       \
    GO(normalmatrix)                                                                                                   \
    GO(mvmatrix)                                                                                                       \
    GO(mvpmatrix)                                                                                                      \
    GO(notexarray)                                                                                                     \
    GO(clean)                                                                                                          \
    GO(clipvertex)                                                                                                     \
    GO2(texs)

void accumShaderNeeds(GLuint shader, shaderconv_need_t* need) {
    CHECK_SHADER(void, shader)
    if (!glshader->converted) return;
#define GO(A)                                                                                                          \
    if (need->need_##A < glshader->need.need_##A) need->need_##A = glshader->need.need_##A;
#define GO2(A) need->need_##A |= glshader->need.need_##A;
    SUPER()
#undef GO
#undef GO2
}

int isShaderCompatible(GLuint shader, shaderconv_need_t* need) {
    CHECK_SHADER(int, shader)
    if (!glshader->converted) return 0;
    /* Matrix uniforms (mvmatrix, mvpmatrix, normalmatrix) are per-program uniforms, NOT inter-stage varyings!
       On ESSL 3.0+, extra vertex outputs not read by fragment are legal; only missing vertex outputs needed
       by fragment (or notexarray mismatch) require recompiling the vertex shader. */
    if (need->need_notexarray != glshader->need.need_notexarray &&
        (need->need_texcoord != -1 || glshader->need.need_texcoord != -1)) {
        return 0;
    }
    if (effective_target_es() >= 300) {
        if (glshader->type != GL_VERTEX_SHADER) {
            return 1;
        }
        if (need->need_color > glshader->need.need_color) return 0;
        if (need->need_secondary > glshader->need.need_secondary) return 0;
        if (need->need_fogcoord > glshader->need.need_fogcoord) return 0;
        if ((need->need_texs & ~glshader->need.need_texs) != 0) return 0;
        if (!need->need_notexarray && need->need_texcoord > glshader->need.need_texcoord) return 0;
        return 1;
    }
    if (need->need_color > glshader->need.need_color) return 0;
    if (need->need_secondary > glshader->need.need_secondary) return 0;
    if (need->need_fogcoord > glshader->need.need_fogcoord) return 0;
    if (need->need_texcoord > glshader->need.need_texcoord) return 0;
    if (need->need_clean > glshader->need.need_clean) return 0;
    if ((need->need_texs & ~glshader->need.need_texs) != 0) return 0;
    return 1;
}
#undef SUPER

void redoShader(GLuint shader, shaderconv_need_t* need) {
    LOAD_GLES2(glShaderSource);
    if (!gles_glShaderSource) return;
    CHECK_SHADER(void, shader)
    if (!glshader->converted) return;
    if (memcmp(&glshader->need, need, sizeof(shaderconv_need_t)) == 0) return;

    memcpy(&glshader->need, need, sizeof(shaderconv_need_t));
    if (is_direct_shader(glshader->source)) {
        free(glshader->converted);
        glshader->converted = strdup(glshader->source);
    } else {
        translate_shader_with_cache(glshader);
    }
    glshader->gles_compiled = 0;
    glshader->compiled = 1;
}

void APIENTRY_GL4ES gl4es_glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* source) {
    DBG(SHUT_LOGD("glGetShaderSource(%d, %d, %p, %p)\n", shader, bufSize, length, source))
    // find shader
    CHECK_SHADER(void, shader)
    if (bufSize <= 0) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    // if no source, then it's an empty string
    if (glshader->source == NULL) {
        noerrorShim();
        if (length) *length = 0;
        source[0] = '\0';
        return;
    }
    // copy concatenated sources
    GLsizei size = strlen(glshader->source);
    if (size + 1 > bufSize) size = bufSize - 1;
    strncpy(source, glshader->source, size);
    source[size] = '\0';
    if (length) *length = size;
    noerrorShim();
}

GLboolean APIENTRY_GL4ES gl4es_glIsShader(GLuint shader) {
    DBG(SHUT_LOGD("glIsShader(%d)\n", shader))
    // find shader
    shader_t* glshader = NULL;
    khint_t k;
    {
        khash_t(shaderlist)* shaders = glstate->glsl->shaders;
        k = kh_get(shaderlist, shaders, shader);
        if (k != kh_end(shaders)) glshader = kh_value(shaders, k);
    }
    return (glshader) ? GL_TRUE : GL_FALSE;
}

shader_t* getShader(GLuint shader) {
    khint_t k;
    {
        khash_t(shaderlist)* shaders = glstate->glsl->shaders;
        k = kh_get(shaderlist, shaders, shader);
        if (k != kh_end(shaders)) return kh_value(shaders, k);
    }
    return NULL;
}

static const char* GLES_NoGLSLSupport = "No Shader support with current backend";

void APIENTRY_GL4ES gl4es_glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog) {
    DBG(SHUT_LOGD("glGetShaderInfoLog(%d, %d, %p, %p)\n", shader, maxLength, length, infoLog))
    // find shader
    CHECK_SHADER(void, shader)
    if (glshader->compiled && !glshader->gles_compiled) {
        if (maxLength > 0 && infoLog) infoLog[0] = '\0';
        if (length) *length = 0;
        noerrorShim();
        return;
    }
    LOAD_GLES2(glGetShaderInfoLog);
    if (gles_glGetShaderInfoLog) {
        gles_glGetShaderInfoLog(glshader->id, maxLength, length, infoLog);
        errorGL();
    } else {
        strncpy(infoLog, GLES_NoGLSLSupport, maxLength);
        if (length) *length = strlen(infoLog);
    }
}

void APIENTRY_GL4ES gl4es_glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    DBG(SHUT_LOGD("glGetShaderiv(%d, %s, %p)\n", shader, PrintEnum(pname), params))
    // find shader
    CHECK_SHADER(void, shader)
    LOAD_GLES2(glGetShaderiv);
    noerrorShim();
    switch (pname) {
    case GL_SHADER_TYPE:
        *params = glshader->type;
        break;
    case GL_DELETE_STATUS:
        *params = (glshader->deleted) ? GL_TRUE : GL_FALSE;
        break;
    case GL_COMPILE_STATUS:
        if (glshader->compiled && !glshader->gles_compiled) {
            *params = GL_TRUE;
        } else if (gles_glGetShaderiv) {
            gles_glGetShaderiv(glshader->id, pname, params);
            errorGL();
        } else {
            *params = GL_FALSE; // stub, compile always fail
        }
        break;
    case GL_INFO_LOG_LENGTH:
        if (glshader->compiled && !glshader->gles_compiled) {
            *params = 0;
        } else if (gles_glGetShaderiv) {
            gles_glGetShaderiv(glshader->id, pname, params);
            errorGL();
        } else {
            *params = strlen(GLES_NoGLSLSupport); // stub, compile always fail
        }
        break;
    case GL_SHADER_SOURCE_LENGTH:
        if (glshader->source)
            *params = strlen(glshader->source) + 1;
        else
            *params = 0;
        break;
    default:
        errorShim(GL_INVALID_ENUM);
    }
}

void APIENTRY_GL4ES gl4es_glGetShaderPrecisionFormat(GLenum shaderType, GLenum precisionType, GLint* range,
                                                     GLint* precision) {
    LOAD_GLES2(glGetShaderPrecisionFormat);
    if (gles_glGetShaderPrecisionFormat) {
        gles_glGetShaderPrecisionFormat(shaderType, precisionType, range, precision);
        errorGL();
    } else {
        errorShim(GL_INVALID_ENUM);
    }
}

void APIENTRY_GL4ES gl4es_glShaderBinary(GLsizei count, const GLuint* shaders, GLenum binaryFormat, const void* binary,
                                         GLsizei length) {
    // TODO: check consistancy of "shaders" values
    LOAD_GLES2(glShaderBinary);
    if (gles_glShaderBinary) {
        gles_glShaderBinary(count, shaders, binaryFormat, binary, length);
        errorGL();
    } else {
        errorShim(GL_INVALID_ENUM);
    }
}

void APIENTRY_GL4ES gl4es_glReleaseShaderCompiler(void) {
    emesis_note_shader_compiler_activity();
    emesis_maybe_reclaim_compiler_memory();
    LOAD_GLES2(glReleaseShaderCompiler);
    if (gles_glReleaseShaderCompiler) {
        gles_glReleaseShaderCompiler();
        errorGL();
    } else
        noerrorShim();
}

// ========== GL_ARB_shader_objects ==============

AliasExport(GLuint, glCreateShader, , (GLenum shaderType));
AliasExport(void, glDeleteShader, , (GLuint shader));
AliasExport(void, glCompileShader, , (GLuint shader));
AliasExport(void, glShaderSource, , (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length));
AliasExport(void, glGetShaderSource, , (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* source));
AliasExport(GLboolean, glIsShader, , (GLuint shader));
AliasExport(void, glGetShaderInfoLog, , (GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog));
AliasExport(void, glGetShaderiv, , (GLuint shader, GLenum pname, GLint* params));
AliasExport(void, glGetShaderPrecisionFormat, ,
            (GLenum shaderType, GLenum precisionType, GLint* range, GLint* precision));
AliasExport(void, glShaderBinary, ,
            (GLsizei count, const GLuint* shaders, GLenum binaryFormat, const void* binary, GLsizei length));
AliasExport_V(void, glReleaseShaderCompiler);

GLhandleARB APIENTRY_GL4ES gl4es_glCreateShaderObject(GLenum shaderType) {
    return gl4es_glCreateShader(shaderType);
}

AliasExport(GLhandleARB, glCreateShaderObject, ARB, (GLenum shaderType));
AliasExport(GLvoid, glShaderSource, ARB,
            (GLhandleARB shaderObj, GLsizei count, const GLcharARB** string, const GLint* length));
AliasExport(GLvoid, glCompileShader, ARB, (GLhandleARB shaderObj));
AliasExport(GLvoid, glGetShaderSource, ARB, (GLhandleARB obj, GLsizei maxLength, GLsizei* length, GLcharARB* source));
