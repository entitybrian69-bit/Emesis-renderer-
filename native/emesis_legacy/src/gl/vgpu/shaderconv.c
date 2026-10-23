//
// Created by serpentspirale on 13/01/2022.
//
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "shaderconv.h"
#include "../string_utils.h"
#include "../logs.h"
#include "../const.h"
#include "../../glx/hardext.h"
#include "../shaderconv.h"
#include "../gl4es.h"

int NO_OPERATOR_VALUE = 9999;

#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

// #define DEBUG
#ifdef DEBUG
#define DBG(a) a
#define DBGLOGD(...) SHUT_LOGD(__VA_ARGS__)
#else
#define DBG(a)
#define DBGLOGD(...)                                                                                                   \
    {}
#endif

void uniform_decls_add(uniform_declarations_list_t* list, const char* variable, const char* initial_value) {
    if (!list || !variable || !variable[0] || !initial_value || !initial_value[0]) return;
    for (int i = 0; i < list->count; ++i) {
        if (list->items[i].variable && strcmp(list->items[i].variable, variable) == 0) {
            char* nv = strdup(initial_value);
            if (nv) {
                free(list->items[i].initial_value);
                list->items[i].initial_value = nv;
            }
            return;
        }
    }
    if (list->count >= list->cap) {
        int new_cap = (list->cap > 0) ? (list->cap * 2) : 16;
        uniform_declaration_s* grown =
            (uniform_declaration_s*)realloc(list->items, (size_t)new_cap * sizeof(uniform_declaration_s));
        if (!grown) return;
        list->items = grown;
        list->cap = new_cap;
    }
    list->items[list->count].variable = strdup(variable);
    list->items[list->count].initial_value = strdup(initial_value);
    if (list->items[list->count].variable && list->items[list->count].initial_value) {
        list->count++;
    } else {
        free(list->items[list->count].variable);
        free(list->items[list->count].initial_value);
    }
}

void uniform_decls_merge(uniform_declarations_list_t* dst, const uniform_declarations_list_t* src) {
    if (!dst || !src) return;
    for (int i = 0; i < src->count; ++i) {
        uniform_decls_add(dst, src->items[i].variable, src->items[i].initial_value);
    }
}

void uniform_decls_clear(uniform_declarations_list_t* list) {
    if (!list) return;
    for (int i = 0; i < list->count; ++i) {
        free(list->items[i].variable);
        free(list->items[i].initial_value);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

int parse_floats_from_string(const char* str, GLfloat* outValues, int maxCount) {
    int count = 0;
    const char* cursor = str;
    const char* lparen = strchr(str, '(');
    if (lparen) cursor = lparen + 1;

    while (*cursor && count < maxCount) {
        if (isalpha((unsigned char)*cursor) || *cursor == '_') {
            while (isalnum((unsigned char)*cursor) || *cursor == '_')
                cursor++;
            continue;
        }
        if (isdigit((unsigned char)*cursor) ||
            ((*cursor == '-' || *cursor == '+' || *cursor == '.') &&
             (isdigit((unsigned char)cursor[1]) || (cursor[1] == '.' && isdigit((unsigned char)cursor[2]))))) {
            char* endptr = NULL;
            outValues[count++] = strtof(cursor, &endptr);
            if (endptr && endptr > cursor) {
                if (*endptr == 'f' || *endptr == 'F') endptr++;
                cursor = endptr;
                continue;
            }
        }
        cursor++;
    }
    return count;
}

int parse_bool_from_string(const char* str) {
    while (*str && isspace((unsigned char)*str)) str++;
    if (strncmp(str, "true", 4) == 0 || strcmp(str, "1") == 0) {
        return GL_TRUE;
    }
    if (strncmp(str, "false", 5) == 0 || strcmp(str, "0") == 0) {
        return GL_FALSE;
    }
    return -1;
}

void set_uniforms_default_value(GLuint program, const uniform_declarations_list_t* uniformList) {
    if (!uniformList || uniformList->count <= 0) return;

    for (int i = 0; i < uniformList->count; i++) {
        const uniform_declaration_s* uniform = &uniformList->items[i];
        if (!uniform->variable || !uniform->variable[0] || !uniform->initial_value || !uniform->initial_value[0]) {
            continue;
        }
        GLint location = gl4es_glGetUniformLocation(program, uniform->variable);
        if (location == -1) {
            continue;
        }

        const char* val_str = uniform->initial_value;
        char decl_type[64] = {0};
        const char* colon = strchr(val_str, ':');
        if (colon && (size_t)(colon - val_str) < sizeof(decl_type)) {
            size_t tlen = (size_t)(colon - val_str);
            memcpy(decl_type, val_str, tlen);
            decl_type[tlen] = '\0';
            val_str = colon + 1;
        }

        const char* check_type = decl_type[0] ? decl_type : val_str;

        if (strstr(check_type, "mat4") != NULL) {
            GLfloat matValues[16] = {0};
            int count = parse_floats_from_string(val_str, matValues, 16);
            if (count == 1) {
                GLfloat d = matValues[0];
                matValues[0] = matValues[5] = matValues[10] = matValues[15] = d;
                gl4es_glUniformMatrix4fv(location, 1, GL_FALSE, matValues);
            } else if (count == 16) {
                gl4es_glUniformMatrix4fv(location, 1, GL_FALSE, matValues);
            }
        } else if (strstr(check_type, "mat3") != NULL) {
            GLfloat matValues[9] = {0};
            int count = parse_floats_from_string(val_str, matValues, 9);
            if (count == 1) {
                GLfloat d = matValues[0];
                matValues[0] = matValues[4] = matValues[8] = d;
                gl4es_glUniformMatrix3fv(location, 1, GL_FALSE, matValues);
            } else if (count == 9) {
                gl4es_glUniformMatrix3fv(location, 1, GL_FALSE, matValues);
            }
        } else if (strstr(check_type, "mat2") != NULL) {
            GLfloat matValues[4] = {0};
            int count = parse_floats_from_string(val_str, matValues, 4);
            if (count == 1) {
                GLfloat d = matValues[0];
                matValues[0] = d; matValues[3] = d;
                gl4es_glUniformMatrix2fv(location, 1, GL_FALSE, matValues);
            } else if (count == 4) {
                gl4es_glUniformMatrix2fv(location, 1, GL_FALSE, matValues);
            }
        } else if (strstr(check_type, "ivec4") != NULL || strstr(check_type, "bvec4") != NULL) {
            GLfloat fvals[4] = {0};
            int count = parse_floats_from_string(val_str, fvals, 4);
            if (count == 1) fvals[1] = fvals[2] = fvals[3] = fvals[0];
            if (count == 1 || count == 4) {
                GLint ivals[4] = {(GLint)fvals[0], (GLint)fvals[1], (GLint)fvals[2], (GLint)fvals[3]};
                gl4es_glUniform4iv(location, 1, ivals);
            }
        } else if (strstr(check_type, "ivec3") != NULL || strstr(check_type, "bvec3") != NULL) {
            GLfloat fvals[3] = {0};
            int count = parse_floats_from_string(val_str, fvals, 3);
            if (count == 1) fvals[1] = fvals[2] = fvals[0];
            if (count == 1 || count == 3) {
                GLint ivals[3] = {(GLint)fvals[0], (GLint)fvals[1], (GLint)fvals[2]};
                gl4es_glUniform3iv(location, 1, ivals);
            }
        } else if (strstr(check_type, "ivec2") != NULL || strstr(check_type, "bvec2") != NULL) {
            GLfloat fvals[2] = {0};
            int count = parse_floats_from_string(val_str, fvals, 2);
            if (count == 1) fvals[1] = fvals[0];
            if (count == 1 || count == 2) {
                GLint ivals[2] = {(GLint)fvals[0], (GLint)fvals[1]};
                gl4es_glUniform2iv(location, 1, ivals);
            }
        } else if (strstr(check_type, "vec4") != NULL) {
            GLfloat vecValues[4] = {0};
            int count = parse_floats_from_string(val_str, vecValues, 4);
            if (count == 1) vecValues[1] = vecValues[2] = vecValues[3] = vecValues[0];
            if (count == 1 || count == 4) {
                gl4es_glUniform4fv(location, 1, vecValues);
            }
        } else if (strstr(check_type, "vec3") != NULL) {
            GLfloat vecValues[3] = {0};
            int count = parse_floats_from_string(val_str, vecValues, 3);
            if (count == 1) vecValues[1] = vecValues[2] = vecValues[0];
            if (count == 1 || count == 3) {
                gl4es_glUniform3fv(location, 1, vecValues);
            }
        } else if (strstr(check_type, "vec2") != NULL) {
            GLfloat vecValues[2] = {0};
            int count = parse_floats_from_string(val_str, vecValues, 2);
            if (count == 1) vecValues[1] = vecValues[0];
            if (count == 1 || count == 2) {
                gl4es_glUniform2fv(location, 1, vecValues);
            }
        } else if (strcmp(check_type, "float") == 0 || strstr(check_type, "float") != NULL) {
            GLfloat v = 0.0f;
            if (parse_floats_from_string(val_str, &v, 1) >= 1) {
                gl4es_glUniform1f(location, v);
            }
        } else if (strcmp(check_type, "int") == 0 || strcmp(check_type, "uint") == 0 ||
                   strstr(check_type, "int") != NULL) {
            GLint value = (GLint)strtol(val_str, NULL, 0);
            gl4es_glUniform1i(location, value);
        } else if (strcmp(check_type, "bool") == 0 || strstr(check_type, "bool") != NULL) {
            GLint value = parse_bool_from_string(val_str);
            if (value != -1) {
                gl4es_glUniform1i(location, value);
            }
        } else if (strstr(check_type, "sampler") != NULL) {
            GLint value = (GLint)strtol(val_str, NULL, 10);
            gl4es_glUniform1i(location, value);
        }
    }
}


int startsWith(char* str, char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static inline int ensure_out_cap(char** buf, size_t* cap, size_t pos, size_t extra) {
    if (pos + extra + 2 <= *cap) return 1;
    size_t new_cap = *cap ? *cap : 4096;
    while (pos + extra + 2 > new_cap) {
        new_cap *= 2;
    }
    char* tmp = (char*)realloc(*buf, new_cap);
    if (!tmp) return 0;
    *buf = tmp;
    *cap = new_cap;
    return 1;
}

char* process_uniform_declarations(char* glslCode, uniform_declarations_list_t* uniformList) {
    if (!glslCode) return NULL;
    if (!strstr(glslCode, "uniform")) {
        return strdup(glslCode);
    }

    size_t srcLen = strlen(glslCode);
    size_t maxLength = srcLen + 512;
    char* modifiedGlslCode = (char*)malloc(maxLength);
    if (!modifiedGlslCode) return strdup(glslCode);

    size_t outIdx = 0;
    const char* cursor = glslCode;
    char name[256], type[256], initial_value[1024], typed_init[1300];

    while (*cursor) {
        if (cursor[0] == '/' && cursor[1] == '/') {
            const char* start = cursor;
            while (*cursor && *cursor != '\n') cursor++;
            if (*cursor == '\n') cursor++;
            size_t seg = (size_t)(cursor - start);
            if (!ensure_out_cap(&modifiedGlslCode, &maxLength, outIdx, seg)) {
                free(modifiedGlslCode);
                return strdup(glslCode);
            }
            memcpy(modifiedGlslCode + outIdx, start, seg);
            outIdx += seg;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            const char* start = cursor;
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/')) cursor++;
            if (*cursor) cursor += 2;
            size_t seg = (size_t)(cursor - start);
            if (!ensure_out_cap(&modifiedGlslCode, &maxLength, outIdx, seg)) {
                free(modifiedGlslCode);
                return strdup(glslCode);
            }
            memcpy(modifiedGlslCode + outIdx, start, seg);
            outIdx += seg;
            continue;
        }
        if (*cursor == '#') {
            const char* start = cursor;
            while (*cursor && *cursor != '\n') {
                if (*cursor == '\\' && cursor[1] == '\n') {
                    cursor += 2;
                    continue;
                }
                cursor++;
            }
            if (*cursor == '\n') cursor++;
            size_t seg = (size_t)(cursor - start);
            if (!ensure_out_cap(&modifiedGlslCode, &maxLength, outIdx, seg)) {
                free(modifiedGlslCode);
                return strdup(glslCode);
            }
            memcpy(modifiedGlslCode + outIdx, start, seg);
            outIdx += seg;
            continue;
        }

        bool prev_boundary = (cursor == glslCode) ||
                             (!isalnum((unsigned char)cursor[-1]) && cursor[-1] != '_');
        if (prev_boundary && strncmp(cursor, "uniform", 7) == 0 && isspace((unsigned char)cursor[7])) {
            const char* cursor_start = cursor;
            const char* p = cursor + 7;
            while (isspace((unsigned char)*p)) p++;

            const char* precision = "";
            if (strncmp(p, "highp", 5) == 0 && isspace((unsigned char)p[5])) {
                precision = " highp";
                p += 5;
                while (isspace((unsigned char)*p)) p++;
            } else if (strncmp(p, "mediump", 7) == 0 && isspace((unsigned char)p[7])) {
                precision = " mediump";
                p += 7;
                while (isspace((unsigned char)*p)) p++;
            } else if (strncmp(p, "lowp", 4) == 0 && isspace((unsigned char)p[4])) {
                precision = " lowp";
                p += 4;
                while (isspace((unsigned char)*p)) p++;
            }

            size_t ti = 0;
            while (isalnum((unsigned char)*p) || *p == '_') {
                if (ti + 1 < sizeof(type)) type[ti++] = *p;
                p++;
            }
            type[ti] = '\0';
            while (isspace((unsigned char)*p)) p++;

            if (precision[0] == '\0') {
                if (strncmp(p, "highp", 5) == 0 && isspace((unsigned char)p[5])) {
                    precision = " highp";
                    p += 5;
                    while (isspace((unsigned char)*p)) p++;
                } else if (strncmp(p, "mediump", 7) == 0 && isspace((unsigned char)p[7])) {
                    precision = " mediump";
                    p += 7;
                    while (isspace((unsigned char)*p)) p++;
                } else if (strncmp(p, "lowp", 4) == 0 && isspace((unsigned char)p[4])) {
                    precision = " lowp";
                    p += 4;
                    while (isspace((unsigned char)*p)) p++;
                }
            }

            if (*p == '{' || ti == 0) {
                if (!ensure_out_cap(&modifiedGlslCode, &maxLength, outIdx, 7)) {
                    free(modifiedGlslCode);
                    return strdup(glslCode);
                }
                memcpy(modifiedGlslCode + outIdx, cursor_start, 7);
                outIdx += 7;
                cursor = cursor_start + 7;
                continue;
            }

            size_t ni = 0;
            bool isArray = false;
            while (isalnum((unsigned char)*p) || *p == '_') {
                if (ni + 1 < sizeof(name)) name[ni++] = *p;
                p++;
            }
            name[ni] = '\0';
            while (isspace((unsigned char)*p)) p++;
            if (*p == '[') isArray = true;

            if (!isArray && ni > 0 && *p == '=') {
                p++;
                size_t vi = 0;
                int paren_depth = 0;
                while (*p && !(*p == ';' && paren_depth == 0)) {
                    if (*p == '(') paren_depth++;
                    else if (*p == ')' && paren_depth > 0) paren_depth--;
                    if (vi + 1 < sizeof(initial_value)) initial_value[vi++] = *p;
                    p++;
                }
                initial_value[vi] = '\0';
                trim(initial_value);
                if (*p == ';') p++;

                if (uniformList && initial_value[0] != '\0') {
                    snprintf(typed_init, sizeof(typed_init), "%s:%s", type, initial_value);
                    uniform_decls_add(uniformList, name, typed_init);
                }

                size_t needed = strlen(precision) + strlen(type) + strlen(name) + 16;
                if (!ensure_out_cap(&modifiedGlslCode, &maxLength, outIdx, needed)) {
                    free(modifiedGlslCode);
                    return strdup(glslCode);
                }
                int written = snprintf(modifiedGlslCode + outIdx, maxLength - outIdx,
                                       "uniform%s %s %s;", precision, type, name);
                if (written > 0) outIdx += (size_t)written;
                cursor = p;
                continue;
            } else if (isArray && ni > 0) {
                const char* arr_start = p;
                while (*p && *p != '=' && *p != ';') p++;
                size_t arr_len = (size_t)(p - arr_start);
                while (*p && *p != ';') p++;
                if (*p == ';') p++;

                size_t needed = strlen(precision) + strlen(type) + strlen(name) + arr_len + 16;
                if (!ensure_out_cap(&modifiedGlslCode, &maxLength, outIdx, needed)) {
                    free(modifiedGlslCode);
                    return strdup(glslCode);
                }
                int written = snprintf(modifiedGlslCode + outIdx, maxLength - outIdx,
                                       "uniform%s %s %s%.*s;", precision, type, name, (int)arr_len, arr_start);
                if (written > 0) outIdx += (size_t)written;
                cursor = p;
                continue;
            }
        }

        if (!ensure_out_cap(&modifiedGlslCode, &maxLength, outIdx, 1)) {
            free(modifiedGlslCode);
            return strdup(glslCode);
        }
        modifiedGlslCode[outIdx++] = *cursor++;
    }

    modifiedGlslCode[outIdx] = '\0';
    return modifiedGlslCode;
}

/**
 * Makes more and more destructive conversions to make the shader compile
 * @return The shader as a string
 */
char* ConvertShaderConditionally(struct shader_s* shader_source) {
    if (hardext.glsl300es) {
        /* On GLES 3.x, always emit ESSL 3.x directly: avoids dummy GPU test compiles AND
           guarantees vertex & fragment shaders share the exact same ESSL version at link time. */
        shader_source->converted = ConvertShader(shader_source->source,
                                                 shader_source->type == GL_VERTEX_SHADER ? 1 : 0,
                                                 &shader_source->need, 1);
        shader_source->converted = ConvertShaderVgpu(shader_source);
    } else {
        int shaderCompileStatus;
        shader_source->converted = ConvertShader(shader_source->source,
                                                 shader_source->type == GL_VERTEX_SHADER ? 1 : 0,
                                                 &shader_source->need, 0);
        shaderCompileStatus = testGenericShader(shader_source);

        if (!shaderCompileStatus && globals4es.vgpu_backport) {
            free(shader_source->converted);
            shader_source->converted = ConvertShader(shader_source->source,
                                                     shader_source->type == GL_VERTEX_SHADER ? 1 : 0,
                                                     &shader_source->need, 0);
            shader_source->converted = ConvertShaderVgpu(shader_source);
        }
    }

    if (!shader_source->converted) return NULL;
    char* processed = process_uniform_declarations(shader_source->converted, &shader_source->uniforms_declarations);
    free(shader_source->converted);
    shader_source->converted = processed;
    return shader_source->converted;
}

/** Convert the shader through multiple steps
 * @param source The start of the shader as a string*/
char* ConvertShaderVgpu(struct shader_s* shader_source) {
    if (!shader_source || !shader_source->converted) return NULL;

    if (globals4es.vgpu_dump) {
        DBGLOGD("New VGPU Shader source:\n%s\n", shader_source->converted);
    }

    // Get the shader source
    char* source = shader_source->converted;
    int sourceLength = strlen(source) + 1;
    // For now, skip stuff
    if (FindString(source, "#version 100")) {
        if (globals4es.vgpu_force_conv || globals4es.vgpu_backport) {
            if (shader_source->type == GL_VERTEX_SHADER) {
                source = ReplaceVariableName(source, &sourceLength, "in", "attribute");
                source = ReplaceVariableName(source, &sourceLength, "out", "varying");
            } else {
                source = ReplaceVariableName(source, &sourceLength, "in", "varying");
                source = ReplaceFragmentOut(source, &sourceLength);
            }

            // Well, we don't have gl_VertexID on OPENGL 1
            source = ReplaceVariableName(source, &sourceLength, "gl_VertexID", "0");
            source = InplaceReplaceSimple(source, &sourceLength, "ivec", "vec");
            source = InplaceReplaceSimple(source, &sourceLength, "bvec", "vec");
            source = InplaceReplaceSimple(source, &sourceLength, "flat ", "");

            source = BackportConstArrays(source, &sourceLength);
            int insertPoint = FindPositionAfterVersion(source);
            source = InplaceInsertByIndex(source, &sourceLength, insertPoint + 1,
                                          "#define texelFetch(a, b, c) vec4(1.0,1.0,1.0,1.0) \n");

            source = ReplaceModOperator(source, &sourceLength);

            if (globals4es.vgpu_dump) {
                DBGLOGD("New VGPU Shader conversion:\n%s\n", source);
            }

            return source;
        }

        // Else, skip the conversion
        if (globals4es.vgpu_dump) {
            DBGLOGD("SKIPPING OLD SHADER CONVERSION \n%s\n", source);
        }
        return source;
    }

    // Remove 'const' storage qualifier
    // printf("REMOVING CONST qualifiers");
    // source = RemoveConstInsideBlocks(source, &sourceLength);
    // source = ReplaceVariableName(source, &sourceLength, "const", " ");

    // Avoid keyword clash with gl4es #define blocks
    // printf("REPLACING KEYWORDS");
    source = InplaceReplaceSimple(source, &sourceLength, "#define texture2D texture\n", "");
    source = ReplaceVariableName(source, &sourceLength, "sample", "vgpu_Sample");
    source = ReplaceVariableName(source, &sourceLength, "texture", "vgpu_texture");

    source = ReplaceFunctionName(source, &sourceLength, "texture2D", "texture");
    source = ReplaceFunctionName(source, &sourceLength, "texture3D", "texture");
    source = ReplaceFunctionName(source, &sourceLength, "texture2DLod", "textureLod");

    // printf("REMOVING \" CHARS ");
    //  " not really supported here
    source = InplaceReplaceSimple(source, &sourceLength, "\"", "");

    // For now let's hope no extensions are used
    // TODO deal with extensions but properly
    // printf("REMOVING EXTENSIONS");
    // source = RemoveUnsupportedExtensions(source);

    // OpenGL natively supports non const global initializers, not OPENGL ES except if we add an extension
    // printf("ADDING EXTENSIONS\n");
    source = InsertExtensions(source, &sourceLength);

    // printf("REPLACING mod OPERATORS");
    //  No support for % operator, so we replace it
    source = ReplaceModOperator(source, &sourceLength);

    // printf("COERCING INT TO FLOATS");
    //  Hey we don't want to deal with implicit type stuff
    source = CoerceIntToFloat(source, &sourceLength);

    // printf("FIXING ARRAY ACCESS");
    //  Avoid any weird type trying to be an index for an array
    source = ForceIntegerArrayAccess(source, &sourceLength);

    // printf("WRAPPING FUNCTION");
    //  Since everything is a float, we need to overload WAY TOO MANY functions
    source = WrapIvecFunctions(source, &sourceLength);

    // printf("REMOVING DUBIOUS DEFINES");
    source = InplaceReplaceSimple(source, &sourceLength, "#define texture texture2D\n", "");
    source = InplaceReplaceSimple(source, &sourceLength, "#define attribute in\n", "");
    source = InplaceReplaceSimple(source, &sourceLength, "#define varying out\n", "");

    if (shader_source->type == GL_VERTEX_SHADER) {
        source = ReplaceVariableName(source, &sourceLength, "attribute", "in");
        source = ReplaceVariableName(source, &sourceLength, "varying", "out");
    } else {
        source = ReplaceVariableName(source, &sourceLength, "varying", "in");
    }

    // Draw buffers aren't dealt the same on OPEN GL|ES
    if (shader_source->type == GL_FRAGMENT_SHADER && doesShaderVersionContainsES(source)) {
        // printf("REPLACING FRAG DATA");
        source = ReplaceGLFragData(source, &sourceLength);
        // printf("REPLACING FRAG COLOR");
        source = ReplaceGLFragColor(source, &sourceLength);
    }

    // printf("FUCKING UP PRECISION");
    source = ReplacePrecisionQualifiers(source, &sourceLength, shader_source->type == GL_VERTEX_SHADER);

    source = ProcessSwitchCases(source, &sourceLength);

    if (globals4es.vgpu_dump) {
        DBGLOGD("New VGPU Shader conversion:\n%s\n", source);
    }

    return source;
}

static const char* switch_template = " switch (%n%[^)] { ";
static const char* case_template = " case %n%[^:] ";
static const char* declaration_template = " const float %s = %s ;";

#define VARIABLE_SIZE 1024
#define MODE_SWITCH 0
#define MODE_CASE 1

/**
 * Convert switches or cases to be usable with the current int to float conversion
 * @param source The shader as a string
 * @param sourceLength The shader allocated length
 * @param mode Whether to scan and fix switches or cases
 * @return The shader as a string, converted appropriately, maybe in a different memory location
 */
char* FindAndCorrect(char* source, int* length, int mode) {
    const char* template = mode == MODE_SWITCH ? switch_template : mode == MODE_CASE ? case_template : NULL;
    char* scan_source = source;
    char template_string[VARIABLE_SIZE];
    size_t string_offset;
    size_t offset = 0;
    unsigned char rewind = 0;
    while (1) {
        int scan_result = sscanf(scan_source, template, &string_offset, &template_string);
        if (scan_result == 0) {
            scan_source++;
            continue;
        } else if (scan_result == EOF) {
            break;
        }
        offset = string_offset + (strstr(scan_source, mode == MODE_SWITCH ? "{"
                                                      : mode == MODE_CASE ? ":"
                                                                          : 0) -
                                  scan_source);  // find it by hand cause sscanf has trouble with two %n operators
        string_offset += (scan_source - source); // convert it from relative to scan to relative to base
        if (mode == MODE_SWITCH && !strstr(template_string, "int(")) { // already cast to int, skip
            size_t insert_end_offset = string_offset + strlen(template_string);
            source = InplaceInsertByIndex(source, length, insert_end_offset, ")");
            source = InplaceInsertByIndex(source, length, string_offset, "int(");
            rewind = 1;
        }
        if (mode == MODE_CASE) {
            if (!isdigit(template_string[0])) { // cant have a number without the first digit, and the standard doesnt
                                                // permit variable names starting with numbers
                char decltemplate_formatted[VARIABLE_SIZE];
                float declared_value = 99;
                snprintf(decltemplate_formatted, VARIABLE_SIZE, declaration_template, template_string, "%f");
                DBGLOGD("Scanning with template %s\n", decltemplate_formatted);
                char* scanbase = source;
                while (1) {
                    int result = sscanf(scanbase, decltemplate_formatted, &declared_value);
                    if (result == 0) {
                        scanbase++;
                        continue;
                    } else if (result == EOF) {
                        DBGLOGD("Scanned the whole shader and didn't find declaration for %s with template \"%s\"\n",
                                template_string, decltemplate_formatted);
                        abort();
                    }
                    break;
                }
                char integer[VARIABLE_SIZE];
                snprintf(integer, VARIABLE_SIZE, "%i", (int)declared_value);
                size_t replace_end_offset = string_offset + strlen(template_string) - 1;
                source = InplaceReplaceByIndex(source, length, string_offset, replace_end_offset, integer);
                rewind = 1;
            }
        }
        if (rewind) {
            scan_source = source; // since inplace replacement operations are destructive, the scan will be rewound
                                  // after doing them
            rewind = 0;
        } else
            scan_source += offset;
    }
    return source;
}

/**
 *Convert switches and cases in the shader to be usable with the current int to float coercion
 * @param source The shader as a string
 * @param sourceLength The shader allocated length
 * @return The shader as a string, converted appropriately, maybe in a different memory location
 */

char* ProcessSwitchCases(char* source, int* length) {
    source = FindAndCorrect(source, length, MODE_SWITCH);
    source = FindAndCorrect(source, length, MODE_CASE);
    return source;
}

/**
 * Turn const arrays and its accesses into a function and function calls
 * @param source The shader as a string
 * @param sourceLength The shader allocated length
 * @return The shader as a string, maybe in a different memory location
 */
char* BackportConstArrays(char* source, int* sourceLength) {
    unsigned long startPoint = strstrPos(source, "const");
    if (startPoint == 0) {
        return source;
    }
    int constStart, constStop;
    GetNextWord(source, startPoint, &constStart, &constStop); // Catch the "const"

    int typeStart, typeStop;
    GetNextWord(source, constStop, &typeStart, &typeStop); // Catch the type, without []

    int variableNameStart, variableNameStop;
    GetNextWord(source, typeStop, &variableNameStart, &variableNameStop); // Catch the var name
    char* variableName = ExtractString(source, variableNameStart, variableNameStop);

    // Now, verify the data type is actually an array
    char* tokenStart = strstr(source + typeStop, "[");
    if (tokenStart != NULL && (tokenStart - source) < variableNameStart) {
        // We've found an array. So we need to get to the starting parenthesis and isolate each member
        int startArray = GetNextTokenPosition(source, variableNameStop, '(', "");
        int endArray = GetClosingTokenPosition(source, startArray);

        // First pass, to count the amount of entries in the array
        int arrayEntryCount = -1;
        int currentPoint = startArray;
        while (currentPoint < endArray) {
            ++arrayEntryCount;
            currentPoint = GetClosingTokenPositionTokenOverride(source, currentPoint, ',');
        }
        if (currentPoint == endArray) {
            ++arrayEntryCount;
        }

        // Now we know how many entries we have, we can copy data
        int entryStart = startArray + 1;
        int entryEnd;
        for (int j = 0; j < arrayEntryCount; ++j) {
            // First, isolate the array entry
            entryEnd = GetClosingTokenPositionTokenOverride(source, entryStart, ',');

            // Replace the entry and jump to the end of the replacement
            source = InplaceReplaceByIndex(source, sourceLength, entryEnd, entryEnd + 1, ";}"); // +2 - 1
            // Build the string to insert
            int indexStringLength = (j == 0 ? 1 : (int)(log10(j) + 1));
            char* replacementString = malloc(19 + indexStringLength + 1);
            replacementString[19 + indexStringLength + 1] = '\0';
            memcpy(replacementString, "if(index==", 10);
            sprintf(replacementString + 10, "%d", j);
            strcpy(replacementString + 10 + indexStringLength, "){return ");

            // Insert the correct index in the replacement string
            source = InplaceInsertByIndex(source, sourceLength, entryStart, replacementString);

            entryStart = entryEnd + 19 + indexStringLength + 2;
            free(replacementString);
        }

        // The replacement string is not needed anymore

        // Add The last "}" to close the function
        source = InplaceInsertByIndex(source, sourceLength, entryStart, "}");
        // Add the argument section of the function
        source = InplaceReplaceByIndex(source, sourceLength, variableNameStop, startArray, "(int index){");
        // Remove the []
        source = InplaceReplaceByIndex(source, sourceLength, typeStop, variableNameStart - 1, " ");
        // Finally, remove the "const" keyword
        source = InplaceReplaceByIndex(source, sourceLength, startPoint, startPoint + 5, " ");

        // Now, we have to turn every array access to a function call
        // TODO change the start position to be more accurate to the end of the function !
        for (int k = strstrPos(source + endArray, variableName) + endArray; k < strlen(source);) {
            int startAccess = GetNextTokenPosition(source, k, '[', "");
            int endAccess = GetClosingTokenPosition(source, startAccess);
            source = InplaceReplaceByIndex(source, sourceLength, endAccess, endAccess, ")");
            source = InplaceReplaceByIndex(source, sourceLength, startAccess, startAccess, "(");

            int nextPos = strstrPos(source + k, variableName) + k;
            if (nextPos == k) break;
            k = nextPos;
        }

        free(variableName);

    } else {
        // Nothing, go to the next loop iteration
    }

    return source;
}

/**
 * Extract a substring from the provided string
 * @param source The shader as a string
 * @param startString The start of the substring
 * @param endString The end of the substring
 * @return A newly allocated substring. Don't forget to free() it !
 */
char* ExtractString(char* source, int startString, int endString) {
    char* subString = malloc((endString - startString) + 1);
    subString[(endString - startString) + 1] = '\0';
    memcpy(subString, source + startString, (endString - startString));
    return subString;
}

/**
 * Replace the out vec4 from a fragment shader by the gl_FragColor constant
 * @param source The shader as a string
 * @return The shader, maybe in a different memory location
 */
char* ReplaceFragmentOut(char* source, int* sourceLength) {
    int startPosition = strstrPos(source, "out");
    if (startPosition == 0) return source; // No "out" keyword
    int t1, t2;
    GetNextWord(source, startPosition, &t1, &t2); // Catches "out"
    GetNextWord(source, t2, &t1, &t2);            // Catches "vec4"
    GetNextWord(source, t2, &t1, &t2);            // Catches the variableName

    // Load the variable inside another string
    char* variableName = malloc(t2 - t1 + 1);
    variableName[t2 - t1 + 1] = '\0';
    memcpy(variableName, source + t1, t2 - t1);

    // Removing the declaration
    source = InplaceReplaceByIndex(source, sourceLength, startPosition, t2 + 1, "");

    // Replacing occurrences of the variable
    source = ReplaceVariableName(source, sourceLength, variableName, "gl_FragColor");

    free(variableName);

    return source;
}

/**
 * Get to the start, then end of the next of current word.
 * @param source The shader as a string
 * @param startPoint The start point to look at
 * @param startWord Will point to the start of the word
 * @param endWord Will point to the end of the word
 */
void GetNextWord(char* source, int startPoint, int* startWord, int* endWord) {
    // Step 1: Find the real start point
    int start = 0;
    while (!start) {
        if (isValidFunctionName(source[startPoint]) || isDigit(source[startPoint])) {
            start = startPoint;
            break;
        }
        ++startPoint;
    }

    // Step 2: Find the end of a word
    int end = 0;
    while (!end) {
        if (!isValidFunctionName(source[startPoint]) && !isDigit(source[startPoint])) {
            end = startPoint;
            break;
        }
        ++startPoint;
    }

    // Then return values
    *startWord = start;
    *endWord = end;
}

char* InsertExtensions(char* source, int* sourceLength) {
    int insertPoint = FindPositionAfterDirectives(source);
    // printf("INSERT POINT: %i\n", insertPoint);

    source = InsertExtension(source, sourceLength, insertPoint + 1, "GL_EXT_shader_non_constant_global_initializers");
    source = InsertExtension(source, sourceLength, insertPoint + 1, "GL_EXT_texture_cube_map_array");
    source = InsertExtension(source, sourceLength, insertPoint + 1, "GL_EXT_texture_buffer");
    source = InsertExtension(source, sourceLength, insertPoint + 1, "GL_OES_texture_storage_multisample_2d_array");
    return source;
}

char* InsertExtension(char* source, int* sourceLength, const int insertPoint, const char* extension) {
    // First, insert the model, then the extension
    source = InplaceInsertByIndex(source, sourceLength, insertPoint,
                                  "#ifdef __EXT__ \n#extension __EXT__ : enable\n#endif\n");
    source = InplaceReplaceSimple(source, sourceLength, "__EXT__", extension);
    return source;
}

int doesShaderVersionContainsES(const char* source) {
    return GetShaderVersion(source) >= 300;
}

char* WrapIvecFunctions(char* source, int* sourceLength) {
    source = WrapFunction(source, sourceLength, "texelFetch", "vgpu_texelFetch",
                          "\nvec4 vgpu_texelFetch(sampler2D sampler, vec2 P, float lod){return texelFetch(sampler, "
                          "ivec2(int(P.x), int(P.y)), int(lod));}\n"
                          "vec4 vgpu_texelFetch(sampler3D sampler, vec3 P, float lod){return texelFetch(sampler, "
                          "ivec3(int(P.x), int(P.y), int(P.z)), int(lod));}\n"
                          "vec4 vgpu_texelFetch(sampler2DArray sampler, vec3 P, float lod){return texelFetch(sampler, "
                          "ivec3(int(P.x), int(P.y), int(P.z)), int(lod));}\n"
                          "#ifdef GL_EXT_texture_buffer\n"
                          "vec4 vgpu_texelFetch(samplerBuffer sampler, float P){return texelFetch(sampler, int(P));}\n"
                          "#endif\n"
                          "#ifdef GL_OES_texture_storage_multisample_2d_array\n"
                          "vec4 vgpu_texelFetch(sampler2DMS sampler, vec2 P, float _sample){return texelFetch(sampler, "
                          "ivec2(int(P.x), int(P.y)), int(_sample));}\n"
                          "vec4 vgpu_texelFetch(sampler2DMSArray sampler, vec3 P, float _sample){return "
                          "texelFetch(sampler, ivec3(int(P.x), int(P.y), int(P.z)), int(_sample));}\n"
                          "#endif\n");

    source = WrapFunction(
        source, sourceLength, "textureSize", "vgpu_textureSize",
        "\nvec2 vgpu_textureSize(sampler2D sampler, float lod){ivec2 size = textureSize(sampler, int(lod));return "
        "vec2(size.x, size.y);}\n"
        "vec3 vgpu_textureSize(sampler3D sampler, float lod){ivec3 size = textureSize(sampler, int(lod));return "
        "vec3(size.x, size.y, size.z);}\n"
        "vec2 vgpu_textureSize(samplerCube sampler, float lod){ivec2 size = textureSize(sampler, int(lod));return "
        "vec2(size.x, size.y);}\n"
        "vec2 vgpu_textureSize(sampler2DShadow sampler, float lod){ivec2 size = textureSize(sampler, int(lod));return "
        "vec2(size.x, size.y);}\n"
        "vec2 vgpu_textureSize(samplerCubeShadow sampler, float lod){ivec2 size = textureSize(sampler, "
        "int(lod));return vec2(size.x, size.y);}\n"
        "#ifdef GL_EXT_texture_cube_map_array\n"
        "vec3 vgpu_textureSize(samplerCubeArray sampler, float lod){ivec3 size = textureSize(sampler, int(lod));return "
        "vec3(size.x, size.y, size.z);}\n"
        "vec3 vgpu_textureSize(samplerCubeArrayShadow sampler, float lod){ivec3 size = textureSize(sampler, "
        "int(lod));return vec3(size.x, size.y, size.z);}\n"
        "#endif\n"
        "vec3 vgpu_textureSize(sampler2DArray sampler, float lod){ivec3 size = textureSize(sampler, int(lod));return "
        "vec3(size.x, size.y, size.z);}\n"
        "vec3 vgpu_textureSize(sampler2DArrayShadow sampler, float lod){ivec3 size = textureSize(sampler, "
        "int(lod));return vec3(size.x, size.y, size.z);}\n"
        "#ifdef GL_EXT_texture_buffer\n"
        "float vgpu_textureSize(samplerBuffer sampler){return float(textureSize(sampler));}\n"
        "#endif\n"
        "#ifdef GL_OES_texture_storage_multisample_2d_array\n"
        "vec2 vgpu_textureSize(sampler2DMS sampler){ivec2 size = textureSize(sampler);return vec2(size.x, size.y);}\n"
        "vec3 vgpu_textureSize(sampler2DMSArray sampler){ivec3 size = textureSize(sampler);return vec3(size.x, size.y, "
        "size.z);}\n"
        "#endif\n");

    source = WrapFunction(
        source, sourceLength, "textureOffset", "vgpu_textureOffset",
        "\nvec4 vgpu_textureOffset(sampler2D tex, vec2 P, vec2 offset, float bias){ivec2 Size = textureSize(tex, "
        "0);return texture(tex, P+offset/vec2(float(Size.x), float(Size.y)), bias);}\n"
        "vec4 vgpu_textureOffset(sampler2D tex, vec2 P, vec2 offset){return vgpu_textureOffset(tex, P, offset, 0.0);}\n"
        "vec4 vgpu_textureOffset(sampler3D tex, vec3 P, vec3 offset, float bias){ivec3 Size = textureSize(tex, "
        "0);return texture(tex, P+offset/vec3(float(Size.x), float(Size.y), float(Size.z)), bias);}\n"
        "vec4 vgpu_textureOffset(sampler3D tex, vec3 P, vec3 offset){return vgpu_textureOffset(tex, P, offset, 0.0);}\n"
        "float vgpu_textureOffset(sampler2DShadow tex, vec3 P, vec2 offset, float bias){ivec2 Size = textureSize(tex, "
        "0);return texture(tex, P+vec3(offset.x, offset.y, 0)/vec3(float(Size.x), float(Size.y), 1.0), bias);}\n"
        "float vgpu_textureOffset(sampler2DShadow tex, vec3 P, vec2 offset){return vgpu_textureOffset(tex, P, offset, "
        "0.0);}\n"
        "vec4 vgpu_textureOffset(sampler2DArray tex, vec3 P, vec2 offset, float bias){ivec3 Size = textureSize(tex, "
        "0);return texture(tex, P+vec3(offset.x, offset.y, 0)/vec3(float(Size.x), float(Size.y), float(Size.z)), "
        "bias);}\n"
        "vec4 vgpu_textureOffset(sampler2DArray tex, vec3 P, vec2 offset){return vgpu_textureOffset(tex, P, offset, "
        "0.0);}\n");

    source = WrapFunction(source, sourceLength, "shadow2D", "vgpu_shadow2D",
                          "\nvec4 vgpu_shadow2D(sampler2DShadow shadow, vec3 coord){return vec4(texture(shadow, "
                          "coord), 0.0, 0.0, 0.0);}\n"
                          "vec4 vgpu_shadow2D(sampler2DShadow shadow, vec3 coord, float bias){return "
                          "vec4(texture(shadow, coord, bias), 0.0, 0.0, 0.0);}\n");
    return source;
}

/**
 * Replace a function and its calls by a wrapper version, only if needed
 * @param source The shader code as a string
 * @param functionName The function to be replaced
 * @param wrapperFunctionName The replacing function name
 * @param function The wrapper function itself
 * @return The shader as a string, maybe in a different memory location
 */
char* WrapFunction(char* source, int* sourceLength, char* functionName, char* wrapperFunctionName,
                   char* wrapperFunction) {
    int originalSize = strlen(source);
    source = ReplaceFunctionName(source, sourceLength, functionName, wrapperFunctionName);
    // If some calls got replaced, add the wrapper
    if (originalSize != strlen(source)) {
        int insertPoint = FindPositionAfterDirectives(source);
        source = InplaceInsertByIndex(source, sourceLength, insertPoint + 1, wrapperFunction);
    }

    return source;
}

/**
 * Replace the % operator with a mathematical equivalent (x - y * floor(x/y))
 * @param source The shader as a string
 * @return The shader as a string, maybe in a different memory location
 */
char* ReplaceModOperator(char* source, int* sourceLength) {
    char* modelString = " mod(x, y) ";
    int startIndex, endIndex = 0;
    int *startPtr = &startIndex, *endPtr = &endIndex;

    for (int i = 0; i < *sourceLength; ++i) {
        if (source[i] != '%') continue;
        // A mod operator is found !
        char* leftOperand = GetOperandFromOperator(source, i, 0, startPtr);
        char* rightOperand = GetOperandFromOperator(source, i, 1, endPtr);

        // Generate a model string to be inserted
        char* replacementString = malloc(strlen(modelString) + 1);
        strcpy(replacementString, modelString);
        int replacementSize = strlen(replacementString);
        replacementString = InplaceReplace(replacementString, &replacementSize, "x", leftOperand);
        replacementString = InplaceReplace(replacementString, &replacementSize, "y", rightOperand);

        // Insert the new string
        source = InplaceReplaceByIndex(source, sourceLength, startIndex, endIndex, replacementString);

        // Free all the temporary strings
        free(leftOperand);
        free(rightOperand);
        free(replacementString);
    }

    return source;
}

/**
 * Change all (u)ints to floats.
 * This is a hack to avoid dealing with implicit conversions on common operators
 * @param source The shader as a string
 * @return The shader as a string, maybe in a new memory location
 * @see ForceIntegerArrayAccess
 */
char* CoerceIntToFloat(char* source, int* sourceLength) {
    // Let's go the "freestyle way"

    // Step 1 is to translate keywords
    // Attempt and loop unrolling -> worked well, time to fix my shit I guess
    source = ReplaceVariableName(source, sourceLength, "int", "float");
    source = WrapFunction(source, sourceLength, "int", "float", "\n ");
    source = ReplaceVariableName(source, sourceLength, "uint", "float");
    source = WrapFunction(source, sourceLength, "uint", "float", "\n ");

    // TODO Yes I could just do the same as above but I'm lazy at times
    source = InplaceReplaceSimple(source, sourceLength, "ivec", "vec");
    source = InplaceReplaceSimple(source, sourceLength, "uvec", "vec");

    source = InplaceReplaceSimple(source, sourceLength, "isampleBuffer", "sampleBuffer");
    source = InplaceReplaceSimple(source, sourceLength, "usampleBuffer", "sampleBuffer");

    source = InplaceReplaceSimple(source, sourceLength, "isampler", "sampler");
    source = InplaceReplaceSimple(source, sourceLength, "usampler", "sampler");

    // Step 3 is slower.
    // We need to parse hardcoded values like 1 and turn it into 1.(0)
    for (int i = 0; i < *sourceLength; ++i) {

        // Avoid version/line directives
        if (source[i] == '#' && (source[i + 1] == 'v' || source[i + 1] == 'l')) {
            // Look for the next line
            while (source[i] != '\n' && source[i] != '\0') {
                i++;
            }
        }

        if (!isDigit(source[i])) {
            continue;
        }
        // So there is a few situations that we have to distinguish:
        // functionName1 (      ----- meaning there is SOMETHING on its left side that is related to the number
        // function(1,          ----- there is something, and it ISN'T related to the number
        // float test=3;        ----- something on both sides, not related to the number.
        // float test=X.2       ----- There is a dot, so it is part of a float already
        // float test = 0.00000 ----- I have to backtrack to find the dot

        if (source[i - 1] == '.' || source[i + 1] == '.') continue; // Number part of a float
        if (isValidFunctionName(source[i - 1])) continue;           // Char attached to something related
        if (isDigit(source[i + 1])) continue;                       // End of number not reached
        if (isDigit(source[i - 1])) {
            // Backtrack to check if the number is floating point
            int shouldBeCoerced = 0;
            for (int j = 1; 1; ++j) {
                if (isDigit(source[i - j])) continue;
                if (isValidFunctionName(source[i - j])) break; // Function or variable name, don't coerce
                if (source[i - j] == '.' || ((source[i - j] == '+' || source[i - j] == '-') &&
                                             (source[i - j - 1] == 'e' || source[i - j - 1] == 'E')))
                    break; // No coercion, float or scientific notation already
                // Nothing found, should be coerced then
                shouldBeCoerced = 1;
                break;
            }

            if (!shouldBeCoerced) continue;
        }

        // Now we know there is nothing related to the digit, turn it into a float
        source = InplaceInsertByIndex(source, sourceLength, i + 1, ".0");
    }

    // TODO Hacks for special built in values and typecasts ?
    source = InplaceReplaceSimple(source, sourceLength, "gl_VertexID", "float(gl_VertexID)");
    source = InplaceReplaceSimple(source, sourceLength, "gl_InstanceID", "float(gl_InstanceID)");

    return source;
}

/** Force all array accesses to use integers by adding an explicit typecast
 * @param source The shader as a string
 * @return The shader as a string, maybe at a new memory location */
char* ForceIntegerArrayAccess(char* source, int* sourceLength) {
    char* markerStart = "$";
    char* markerEnd = "`";

    // Step 1, we need to mark all [] that are empty and must not be changed
    int leftCharIndex = 0;
    for (int i = 0; i < *sourceLength; ++i) {
        if (source[i] == '[') {
            leftCharIndex = i;
            continue;
        }
        // If a start has been found
        if (leftCharIndex) {
            if (source[i] == ' ' || source[i] == '\n') {
                continue;
            }
            // We find the other side and mark both ends
            if (source[i] == ']') {
                source[leftCharIndex] = *markerStart;
                source[i] = *markerEnd;
            }
        }
        // Something else is there, abort the marking phase for this one
        leftCharIndex = 0;
    }

    // Step 2, replace the array accesses with a forced typecast version
    source = InplaceReplaceSimple(source, sourceLength, "]", ")]");
    source = InplaceReplaceSimple(source, sourceLength, "[", "[int(");

    // Step 3, restore all marked empty []
    source = InplaceReplaceSimple(source, sourceLength, markerStart, "[");
    source = InplaceReplaceSimple(source, sourceLength, markerEnd, "]");

    return source;
}

/** Small helper to help evaluate whether to continue or not I guess
 * Values over 9900 are not for real operators, more like stop indicators*/
int GetOperatorValue(char operator) {
    if (operator == ',' || operator == ';') return 9998;
    if (operator == '=') return 9997;
    if (operator == '+' || operator == '-') return 3;
    if (operator == '*' || operator == '/' || operator == '%') return 2;
    return NO_OPERATOR_VALUE; // Meaning no value;
}

/** Get the left or right operand, given the last index of the operator
 * It bases its ability to get operands by evaluating the priority of operators.
 * @param source The shader as a string
 * @param operatorIndex The index the operator is found
 * @param rightOperand Whether we get the right or left operator
 * @param limit The left or right index of the operand
 * @return newly allocated string with the operand
 */
char* GetOperandFromOperator(char* source, int operatorIndex, int rightOperand, int* limit) {
    int parserState = 0;
    int parserDirection = rightOperand ? 1 : -1;
    int operandStartIndex = 0, operandEndIndex = 0;
    int parenthesesLeft = 0, hasFoundParentheses = 0;
    int operatorValue = GetOperatorValue(source[operatorIndex]);
    int lastOperator = 0; // Used to determine priority for unary operators

    char parenthesesStart = rightOperand ? '(' : ')';
    char parenthesesEnd = rightOperand ? ')' : '(';
    int stringIndex = operatorIndex;

    // Get to the operand
    while (parserState == 0) {
        stringIndex += parserDirection;
        if (source[stringIndex] != ' ') {
            parserState = 1;
            // Place the mark
            if (rightOperand) {
                operandStartIndex = stringIndex;
            } else {
                operandEndIndex = stringIndex;
            }

            // Special case for unary operator when parsing to the right
            if (GetOperatorValue(source[stringIndex]) == 3) { // 3 is +- operators
                stringIndex += parserDirection;
            }
        }
    }

    // Get to the other side of the operand, the twist is here.
    while (parenthesesLeft > 0 || parserState == 1) {

        // Look for parentheses
        if (source[stringIndex] == parenthesesStart) {
            hasFoundParentheses = 1;
            parenthesesLeft += 1;
            stringIndex += parserDirection;
            continue;
        }

        if (source[stringIndex] == parenthesesEnd) {
            hasFoundParentheses = 1;
            parenthesesLeft -= 1;

            // Likely to happen in a function call
            if (parenthesesLeft < 0) {
                parserState = 3;
                if (rightOperand) {
                    operandEndIndex = stringIndex - 1;
                } else {
                    operandStartIndex = stringIndex + 1;
                }
                continue;
            }
            stringIndex += parserDirection;
            continue;
        }

        // Small optimisation
        if (parenthesesLeft > 0) {
            stringIndex += parserDirection;
            continue;
        }

        // So by now the following assumptions are made
        // 1 - We aren't between parentheses
        // 2 - No implicit multiplications are present
        // 3 - No fuckery with operators like "test = +-+-+-+-+-+-+-+-3;" although I attempt to support them

        // Higher value operators have less priority
        int currentValue = GetOperatorValue(source[stringIndex]);

        // The condition is different due to the evaluation order which is left to right, aside from the unary operators
        if ((rightOperand ? currentValue >= operatorValue : currentValue > operatorValue)) {
            if (currentValue == NO_OPERATOR_VALUE) {
                if (source[stringIndex] == ' ') {
                    stringIndex += parserDirection;
                    continue;
                }

                // Found an operand, so reset the operator eval for unary
                if (rightOperand) lastOperator = NO_OPERATOR_VALUE;

                // maybe it is the start of a function ?
                if (hasFoundParentheses) {
                    parserState = 2;
                    continue;
                }
                // If no full () set is found, assume we didn't fully travel the operand
                stringIndex += parserDirection;
                continue;
            }

            // Special case when parsing unary operator to the right
            if (rightOperand && operatorValue == 3 && lastOperator < currentValue) {
                stringIndex += parserDirection;
                continue;
            }

            // Stop, we found an operator of same worth.
            parserState = 3;
            if (rightOperand) {
                operandEndIndex = stringIndex - 1;
            } else {
                operandStartIndex = stringIndex + 1;
            }
        }

        // Special case for unary operators from the right
        if (rightOperand && operatorValue == 3) { // 3 is + - operators
            lastOperator = currentValue;
        } // Special case for unary operators from the left
        if (!rightOperand && operatorValue < 3 && currentValue == 3) {
            lastOperator = NO_OPERATOR_VALUE;
            for (int j = 1; 1; ++j) {
                int subCurrentValue = GetOperatorValue(source[stringIndex - j]);
                if (subCurrentValue != NO_OPERATOR_VALUE) {
                    lastOperator = subCurrentValue;
                    continue;
                }

                // No operator value, can be almost anything
                if (source[stringIndex - j] == ' ') continue;
                // Else we found something. Did we found a high priority operator ?
                if (lastOperator <= operatorValue) { // If so, we allow continuing and going out of the loop
                    stringIndex -= j;
                    parserState = 1;
                    break;
                }
                // No other operator found
                operandStartIndex = stringIndex;
                parserState = 3;
                break;
            }
        }
        stringIndex += parserDirection;
    }

    // Status when we get the name of a function and nothing else.
    while (parserState == 2) {
        if (source[stringIndex] != ' ') {
            stringIndex += parserDirection;
            continue;
        }
        if (rightOperand) {
            operandEndIndex = stringIndex - 1;
        } else {
            operandStartIndex = stringIndex + 1;
        }
        parserState = 3;
    }

    // At this point, we know both the start and end point of our operand, let's copy it
    char* operand = malloc(operandEndIndex - operandStartIndex + 2);
    memcpy(operand, source + operandStartIndex, operandEndIndex - operandStartIndex + 1);
    // Make sure the string is null terminated
    operand[operandEndIndex - operandStartIndex + 1] = '\0';

    // Send back the limitIndex
    *limit = rightOperand ? operandEndIndex : operandStartIndex;

    return operand;
}

static int is_ident(char c) {
    return (c == '_' || isalnum((unsigned char)c));
}

static int find_out_var_name_for_location(const char* source, int location, char* outName, int outNameSize) {
    const char* p = source;
    while ((p = strstr(p, "layout")) != NULL) {
        const char* open = strchr(p, '(');
        if (!open) {
            p += 6;
            continue;
        }
        const char* close = strchr(open, ')');
        if (!close) {
            p += 6;
            continue;
        }

        const char* loc = strstr(open, "location");
        if (!loc || loc > close) {
            p = close + 1;
            continue;
        }

        const char* eq = strchr(loc, '=');
        if (!eq || eq > close) {
            p = close + 1;
            continue;
        }
        const char* nump = eq + 1;
        while (nump < close && isspace((unsigned char)*nump))
            ++nump;
        if (nump >= close || !isdigit((unsigned char)*nump)) {
            p = close + 1;
            continue;
        }
        char* endptr;
        long val = strtol(nump, &endptr, 10);
        if (val != location) {
            p = close + 1;
            continue;
        }

        const char* after = close + 1;
        while (*after && isspace((unsigned char)*after))
            ++after;
        const char* outTok = strstr(after, "out");
        if (!outTok) {
            p = close + 1;
            continue;
        }
        if (outTok != after) {
            int allspace = 1;
            for (const char* q = after; q < outTok; ++q)
                if (!isspace((unsigned char)*q)) {
                    allspace = 0;
                    break;
                }
            if (!allspace) {
                p = close + 1;
                continue;
            }
        }
        const char* cur = outTok + 3;
        const char* semi = strchr(cur, ';');
        if (!semi) {
            p = close + 1;
            continue;
        }
        const char* token = cur;
        const char* foundIdent = NULL;
        while (token < semi) {
            while (token < semi && isspace((unsigned char)*token))
                ++token;
            if (token >= semi) break;
            if (is_ident(*token)) {
                const char* start = token;
                while (token < semi && is_ident(*token))
                    ++token;
                foundIdent = start;
                // continue scanning
            } else {
                ++token;
            }
        }
        if (foundIdent) {
            int len = 0;
            const char* it = foundIdent;
            while (it < semi && is_ident(*it) && len + 1 < outNameSize) {
                outName[len++] = *it++;
            }
            outName[len] = '\0';
            if (len > 0) return 1;
        }
        p = close + 1;
    }
    return 0;
}

/**
 * Replace any gl_FragData[n] reference by creating an out variable with the manual layout binding
 * @param source  The shader source as a string
 * @return The shader as a string, maybe at a different memory location
 */
char* ReplaceGLFragData(char* source, int* sourceLength) {
    int esShader = doesShaderVersionContainsES(source);
    int glslVer = GetShaderVersion(source);
    int needExplicitLocExt = (!esShader && glslVer < 330);
    int addedExplicitLocExt = 0;

    for (int i = 0; i < 16; ++i) {
        char needle[40];
        sprintf(needle, "gl_FragData[%i]", i);

        char* useFragData = strstr(source, needle);
        if (useFragData == NULL) {
            sprintf(needle, "gl_FragData[int(%i.0)]", i);
            useFragData = strstr(source, needle);
            if (useFragData == NULL) continue;
        }

        if (needExplicitLocExt && !addedExplicitLocExt &&
            !strstr(source, "GL_ARB_explicit_attrib_location")) {
            int verEnd = FindPositionAfterVersion(source);
            int verIns = (source[verEnd] == '\n') ? (verEnd + 1) : verEnd;
            source = InplaceInsertByIndex(source, sourceLength, verIns,
                                          "#extension GL_ARB_explicit_attrib_location : enable\n");
            addedExplicitLocExt = 1;
        }

        char existingName[64] = {0};
        int haveExisting = find_out_var_name_for_location(source, i, existingName, sizeof(existingName));

        if (haveExisting) {
            source = InplaceReplaceSimple(source, sourceLength, needle, existingName);
        } else {
            char replacement[32];
            char replacementLine[120];
            sprintf(replacement, "vgpu_FragData%i", i);
            if (esShader) {
                sprintf(replacementLine, "layout(location = %i) out highp vec4 %s;\n", i, replacement);
            } else {
                sprintf(replacementLine, "layout(location = %i) out vec4 %s;\n", i, replacement);
            }
            source = InplaceReplaceSimple(source, sourceLength, needle, replacement);
            int insertPoint = FindPositionAfterDirectives(source);
            int insIdx = (source[insertPoint] == '\n') ? (insertPoint + 1) : insertPoint;
            source = InplaceInsertByIndex(source, sourceLength, insIdx, replacementLine);
        }
    }
    return source;
}

/**
 * Replace the gl_FragColor
 * @param source The shader as a string
 * @return The shader a a string, maybe in a different memory location
 */
char* ReplaceGLFragColor(char* source, int* sourceLength) {
    if (strstr(source, "gl_FragColor")) {
        int esShader = doesShaderVersionContainsES(source);
        int glslVer = GetShaderVersion(source);
        if (!esShader && glslVer < 330 && !strstr(source, "GL_ARB_explicit_attrib_location")) {
            int verEnd = FindPositionAfterVersion(source);
            int verIns = (source[verEnd] == '\n') ? (verEnd + 1) : verEnd;
            source = InplaceInsertByIndex(source, sourceLength, verIns,
                                          "#extension GL_ARB_explicit_attrib_location : enable\n");
        }
        source = InplaceReplaceSimple(source, sourceLength, "gl_FragColor", "vgpu_FragColor");
        int insertPoint = FindPositionAfterDirectives(source);
        int insIdx = (source[insertPoint] == '\n') ? (insertPoint + 1) : insertPoint;
        if (esShader) {
            source = InplaceInsertByIndex(source, sourceLength, insIdx,
                                          "layout(location = 0) out highp vec4 vgpu_FragColor;\n");
        } else {
            source = InplaceInsertByIndex(source, sourceLength, insIdx,
                                          "layout(location = 0) out vec4 vgpu_FragColor;\n");
        }
    }
    return source;
}

/**
 * Remove all extensions right now by replacing them with spaces
 * @param source The shader as a string
 * @return The shader as a string, maybe in a different memory location
 */
char* RemoveUnsupportedExtensions(char* source) {
    // TODO remove only specific extensions ?
    for (char* extensionPtr = strstr(source, "#extension "); extensionPtr;
         extensionPtr = strstr(source, "#extension ")) {
        int i = 0;
        while (extensionPtr[i] != '\n') {
            extensionPtr[i] = ' ';
            ++i;
        }
    }
    return source;
}

/**
 * Replace the variable name in a shader, mostly used to avoid keyword clashing
 * @param source The shader as a string
 * @param initialName The initial name for the variable
 * @param newName The new name for the variable
 * @return The shader as a string, maybe in a different memory location
 */
static inline int is_var_char_before(unsigned char c) {
    switch (c) {
        case '{': case '}': case '(': case '[': case ']':
        case ';': case '+': case '-': case '*': case '/':
        case '~': case '!': case '%': case '<': case '>':
        case ',': case '&': case '|': case '=':
        case ' ': case '\n': case '\t': case '\r':
            return 1;
        default:
            return 0;
    }
}

static inline int is_var_char_after(unsigned char c) {
    switch (c) {
        case ')': case '[': case ']': case ';': case '+':
        case '-': case '*': case '/': case '%': case '<':
        case '>': case ',': case '|': case '&': case '.':
        case '=': case ' ': case '\n': case '\t': case '\r':
            return 1;
        default:
            return 0;
    }
}

char* ReplaceVariableName(char* source, int* sourceLength, char* initialName, char* newName) {
    if (!source || !initialName || !initialName[0] || !newName) return source;
    char* first = strstr(source, initialName);
    if (!first) return source;

    size_t lS = strlen(initialName), lD = strlen(newName);
    int matches = 0;
    const char* scan = first;
    while (scan) {
        if (scan > source && is_var_char_before((unsigned char)scan[-1]) &&
            is_var_char_after((unsigned char)scan[lS])) {
            matches++;
        }
        scan = strstr(scan + lS, initialName);
    }
    if (matches == 0) return source;

    if (lD == lS) {
        char* p = first;
        while (p) {
            if (p > source && is_var_char_before((unsigned char)p[-1]) &&
                is_var_char_after((unsigned char)p[lS])) {
                memcpy(p, newName, lD);
            }
            p = strstr(p + lS, initialName);
        }
        return source;
    }

    size_t srcLen = strlen(source);
    size_t newLen = (size_t)((long)srcLen + ((long)lD - (long)lS) * (long)matches);
    int allocCap = *sourceLength;
    if ((int)(newLen + 1) > allocCap) {
        allocCap = (int)(newLen + 1) * 2;
    }
    char* out = (char*)malloc((size_t)allocCap);
    if (!out) return source;

    const char* cur = source;
    const char* hit = first;
    char* dst = out;
    while (hit) {
        if (hit > source && is_var_char_before((unsigned char)hit[-1]) &&
            is_var_char_after((unsigned char)hit[lS])) {
            size_t chunk = (size_t)(hit - cur);
            if (chunk) {
                memcpy(dst, cur, chunk);
                dst += chunk;
            }
            if (lD) {
                memcpy(dst, newName, lD);
                dst += lD;
            }
            cur = hit + lS;
            hit = strstr(cur, initialName);
        } else {
            hit = strstr(hit + lS, initialName);
        }
    }
    size_t tail = (source + srcLen) - cur;
    if (tail) {
        memcpy(dst, cur, tail);
        dst += tail;
    }
    *dst = '\0';
    free(source);
    *sourceLength = allocCap;
    return out;
}

static inline int has_open_paren_after_ws(const char* p) {
    while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') ++p;
    return (*p == '(');
}

/**
 * Replace a function definition and calls to the function to another name
 * @param source The shader as a string
 * @param sourceLength The shader length
 * @param initialName The name be to changed
 * @param finalName The name to use instead
 * @return The shader as a string, maybe in a different memory location
 */
char* ReplaceFunctionName(char* source, int* sourceLength, char* initialName, char* finalName) {
    if (!source || !initialName || !initialName[0] || !finalName) return source;
    char* first = strstr(source, initialName);
    if (!first) return source;

    size_t lS = strlen(initialName), lD = strlen(finalName);
    int matches = 0;
    const char* scan = first;
    while (scan) {
        if ((scan == source || !isValidFunctionName(scan[-1])) && has_open_paren_after_ws(scan + lS)) {
            matches++;
        }
        scan = strstr(scan + lS, initialName);
    }
    if (matches == 0) return source;

    if (lD == lS) {
        char* p = first;
        while (p) {
            if ((p == source || !isValidFunctionName(p[-1])) && has_open_paren_after_ws(p + lS)) {
                memcpy(p, finalName, lD);
            }
            p = strstr(p + lS, initialName);
        }
        return source;
    }

    size_t srcLen = strlen(source);
    size_t newLen = (size_t)((long)srcLen + ((long)lD - (long)lS) * (long)matches);
    int allocCap = *sourceLength;
    if ((int)(newLen + 1) > allocCap) {
        allocCap = (int)(newLen + 1) * 2;
    }
    char* out = (char*)malloc((size_t)allocCap);
    if (!out) return source;

    const char* cur = source;
    const char* hit = first;
    char* dst = out;
    while (hit) {
        if ((hit == source || !isValidFunctionName(hit[-1])) && has_open_paren_after_ws(hit + lS)) {
            size_t chunk = (size_t)(hit - cur);
            if (chunk) {
                memcpy(dst, cur, chunk);
                dst += chunk;
            }
            if (lD) {
                memcpy(dst, finalName, lD);
                dst += lD;
            }
            cur = hit + lS;
            hit = strstr(cur, initialName);
        } else {
            hit = strstr(hit + lS, initialName);
        }
    }
    size_t tail = (source + srcLen) - cur;
    if (tail) {
        memcpy(dst, cur, tail);
        dst += tail;
    }
    *dst = '\0';
    free(source);
    *sourceLength = allocCap;
    return out;
}

/** Remove 'const ' storage qualifier from variables inside {..} blocks
 * @param source The pointer to the start of the shader */
char* RemoveConstInsideBlocks(char* source, int* sourceLength) {
    int insideBlock = 0;
    char* keyword = "const \0";
    int keywordLength = strlen(keyword);

    for (int i = 0; i < *sourceLength + 1; ++i) {
        // Step 1, look for a block
        if (source[i] == '{') {
            insideBlock += 1;
            continue;
        }
        if (!insideBlock) continue;

        // A block has been found, look for the keyword
        if (source[i] == keyword[0]) {
            int keywordMatch = 1;
            int j = 1;
            while (j < keywordLength) {
                if (source[i + j] != keyword[j]) {
                    keywordMatch = 0;
                    break;
                }
                j += 1;
            }
            // A match is found, remove it
            if (keywordMatch) {
                source = InplaceReplaceByIndex(source, sourceLength, i, i + j - 1, " ");
                continue;
            }
        }

        // Look for an exit
        if (source[i] == '}') {
            insideBlock -= 1;
            continue;
        }
    }
    return source;
}

/** Find the first point which is right after most shader directives
 * @param source The shader as a string
 * @return The index position after the #version line, start of the shader if not found
 */
int FindPositionAfterVersion(char* source) {
    if (!source) return 0;
    const char* p = source;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        const char* line = p;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "version", 7) == 0 && (isspace((unsigned char)p[7]) || p[7] == '\0')) {
                while (*p && *p != '\n') p++;
                return (int)(p - source);
            }
        }
        (void)line;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return 0;
}

int FindPositionAfterDirectives(char* source) {
    if (!source) return 0;
    int lastDirectiveEnd = FindPositionAfterVersion(source);
    int if_depth = 0;
    int directive_in_if = 0;
    const char* p = source;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#') {
            const char* h = p + 1;
            while (*h == ' ' || *h == '\t') h++;
            if (strncmp(h, "if", 2) == 0 &&
                (isspace((unsigned char)h[2]) || strncmp(h, "ifdef", 5) == 0 || strncmp(h, "ifndef", 6) == 0)) {
                if_depth++;
            } else if (strncmp(h, "endif", 5) == 0 && (isspace((unsigned char)h[5]) || h[5] == '\0' || h[5] == '/')) {
                if (if_depth > 0) {
                    if_depth--;
                    if (if_depth == 0 && directive_in_if) {
                        while (*h && *h != '\n') h++;
                        lastDirectiveEnd = (int)(h - source);
                        directive_in_if = 0;
                        p = (*h == '\n') ? (h + 1) : h;
                        continue;
                    }
                }
            } else if (strncmp(h, "extension", 9) == 0 && isspace((unsigned char)h[9])) {
                while (*h && *h != '\n') h++;
                lastDirectiveEnd = (int)(h - source);
                if (if_depth > 0) directive_in_if = 1;
                p = (*h == '\n') ? (h + 1) : h;
                continue;
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return lastDirectiveEnd;
}

/**
 * Replace and inserts precision qualifiers as necessary, see LIBGL_VGPU_PRECISION
 * @param source The shader as a string
 * @param sourceLength The length of the string
 * @return The shader as a string, maybe in a different memory location
 */
char* ReplacePrecisionQualifiers(char* source, int* sourceLength, int isVertex) {

    if (!doesShaderVersionContainsES(source)) {
        if (globals4es.vgpu_dump) {
            DBGLOGD("\nSKIPPING the replacement qualifiers step\n");
        }
        return source;
    }

    // Step 1 is to remove any "precision" qualifiers
    for (unsigned long currentPosition = strstrPos(source, "precision "); currentPosition > 0;
         currentPosition = strstrPos(source, "precision ")) {
        // Once a qualifier is found, get to the end of the instruction and replace
        int endPosition = GetNextTokenPosition(source, currentPosition, ';', "");
        source = InplaceReplaceByIndex(source, sourceLength, currentPosition, endPosition, "");
    }

    // Step 2 is to insert precision qualifiers, even the ones we think are defaults, since there are defaults only for
    // some types

    int insertPoint = FindPositionAfterDirectives(source);
    source = InplaceInsertByIndex(source, sourceLength, insertPoint,
                                  "\nprecision lowp sampler2D;\n"
                                  "precision lowp sampler3D;\n"
                                  "precision lowp sampler2DShadow;\n"
                                  "precision lowp samplerCubeShadow;\n"
                                  "precision lowp sampler2DArray;\n"
                                  "precision lowp sampler2DArrayShadow;\n"
                                  "precision lowp samplerCube;\n"
                                  "#ifdef GL_EXT_texture_buffer\n"
                                  "precision lowp samplerBuffer;\n"
                                  "precision lowp imageBuffer;\n"
                                  "#endif\n"
                                  "#ifdef GL_EXT_texture_cube_map_array\n"
                                  "precision lowp imageCubeArray;\n"
                                  "precision lowp samplerCubeArray;\n"
                                  "precision lowp samplerCubeArrayShadow;\n"
                                  "#endif\n"
                                  "#ifdef GL_OES_texture_storage_multisample_2d_array\n"
                                  "precision lowp sampler2DMS;\n"
                                  "precision lowp sampler2DMSArray;\n"
                                  "#endif\n");

    if (GetShaderVersion(source) > 300) {
        source = InplaceInsertByIndex(source, sourceLength, insertPoint,
                                      "\nprecision lowp image2D;\n"
                                      "precision lowp image2DArray;\n"
                                      "precision lowp image3D;\n"
                                      "precision lowp imageCube;\n");
    }
    int supportHighp = ((isVertex || hardext.highp) ? 1 : 0);
    source = InplaceInsertByIndex(source, sourceLength, insertPoint,
                                  supportHighp ? "\nprecision highp float;\n" : "\nprecision mediump float;\n");

    if (globals4es.vgpu_precision != 0) {
        char* target_precision;
        switch (globals4es.vgpu_precision) {
        case 1:
            target_precision = "highp";
            break;
        case 2:
            target_precision = "mediump";
            break;
        case 3:
            target_precision = "lowp";
            break;
        default:
            target_precision = "highp";
        }
        source = ReplaceVariableName(source, sourceLength, "highp", target_precision);
        source = ReplaceVariableName(source, sourceLength, "mediump", target_precision);
        source = ReplaceVariableName(source, sourceLength, "lowp", target_precision);
    }

    return source;
}

/**
 * @param openingToken The opening token
 * @return All closing tokens, if available
 */
char* GetClosingTokens(char openingToken) {
    switch (openingToken) {
    case '(':
        return ")";
    case '[':
        return "]";
    case ',':
        return ",)";
    case '{':
        return "}";
    case ';':
        return ";";

    default:
        return "";
    }
}

/**
 * @param openingToken The opening token
 * @return Whether the token is an opening token
 */
int isOpeningToken(char openingToken) {
    return openingToken != ',' && strlen(GetClosingTokens(openingToken)) != 0;
}

int GetClosingTokenPosition(const char* source, int initialTokenPosition) {
    return GetClosingTokenPositionTokenOverride(source, initialTokenPosition, source[initialTokenPosition]);
}

/**
 * Get the index of the closing token within a string, same as initialTokenPosition if not found
 * @param source The string to look into
 * @param initialTokenPosition The opening token position
 * @return The closing token position
 */
int GetClosingTokenPositionTokenOverride(const char* source, int initialTokenPosition, char initialToken) {
    // Step 1: Determine the closing token
    char openingToken = initialToken;
    char* closingTokens = GetClosingTokens(openingToken);
    DBGLOGD("Closing tokens: %s", closingTokens);
    if (strlen(closingTokens) == 0) {
        DBGLOGD("No closing tokens, somehow \n");
        return initialTokenPosition;
    }

    // Step 2: Go through the string to find what we want
    for (int i = initialTokenPosition + 1; i < strlen(source); ++i) {
        // Loop though all the available closing tokens first, since opening/closing tokens can be identical
        for (int j = 0; j < strlen(closingTokens); ++j) {
            if (source[i] == closingTokens[j]) {
                return i;
            }
        }

        if (isOpeningToken(source[i])) {
            i = GetClosingTokenPosition(source, i);
            continue;
        }
    }
    DBGLOGD("No closing tokens 2 , somehow \n");
    return initialTokenPosition; // Nothing found
}

/**
 * Return the position of the first token corresponding to what we want
 * @param source The source string
 * @param initialPosition The starting position to look from
 * @param token The token you want to find
 * @param acceptedChars All chars we can go over without tripping. Empty means all chars are allowed.
 * @return
 */
int GetNextTokenPosition(const char* source, int initialPosition, const char token, const char* acceptedChars) {
    for (int i = initialPosition + 1; i < strlen(source); ++i) {
        // Tripping check
        if (strlen(acceptedChars) > 0) {
            for (int j = 0; j < strlen(acceptedChars); ++j) {
                if (source[i] == acceptedChars[j]) break; // No tripping, continue
            }
            return initialPosition; // Tripped, meaning the token is not found
        }

        if (source[i] == token) {
            return i;
        }
    }
    return initialPosition;
}

/**
 * @param haystack
 * @param needle
 * @return The position of the first occurence of the needle in the haystack
 */
unsigned long strstrPos(const char* haystack, const char* needle) {
    char* substr = strstr(haystack, needle);
    if (substr == NULL) return 0;
    return (substr - haystack);
}

/**
 * Inserts int(...) on a specific argument from each call to the function
 * @param source The shader as a string
 * @param functionName The name of the function to manipulate
 * @param argumentPosition The position of the argument to manipulate, from 0. If not found, no changes are made.
 * @return The shader as a string, maybe in a different memory location
 */
char* insertIntAtFunctionCall(char* source, int* sourceSize, const char* functionName, int argumentPosition) {
    // TODO a less naive function for edge-cases ?
    unsigned long functionCallPosition = strstrPos(source, functionName);
    while (functionCallPosition != 0) {
        int openingTokenPosition =
            GetNextTokenPosition(source, functionCallPosition + strlen(functionName), '(', " \n\r\t");
        if (source[openingTokenPosition] == '(') {
            // Function call found, determine the start and end of the argument
            int endArgPos = openingTokenPosition;
            int startArgPos = openingTokenPosition;

            // Note the additional check to see we aren't at the end of a function
            for (int argCount = 0; argCount <= argumentPosition && source[startArgPos] != ')'; ++argCount) {
                endArgPos = GetClosingTokenPositionTokenOverride(source, endArgPos, ',');
                if (argCount == argumentPosition) {
                    // Argument found, insert the int(...)
                    source = InplaceInsertByIndex(source, sourceSize, endArgPos + 1, ")");
                    source = InplaceInsertByIndex(source, sourceSize, startArgPos + 1, "int(");
                    break;
                }
                // Not the arg we want, got to the next one
                startArgPos = endArgPos;
            }
        }

        // Get the next function call
        unsigned long offset = strstrPos(source + functionCallPosition + strlen(functionName), functionName);
        if (offset == 0) break; // No more function calls
        functionCallPosition += offset + strlen(functionName);
    }
    return source;
}

/**
 * @param source The shader as a string
 * @return The shader version: eg. 310 for #version 310 es
 */
int GetShaderVersion(const char* source) {
    // Oh yeah, I won't care much about this function
    if (FindString(source, "#version 320 es")) {
        return 320;
    }
    if (FindString(source, "#version 310 es")) {
        return 310;
    }
    if (FindString(source, "#version 300 es")) {
        return 300;
    }

    // Check for core profile versions
    if (FindString(source, "#version 460 core")) return 460;
    if (FindString(source, "#version 450 core")) return 450;
    if (FindString(source, "#version 440 core")) return 440;
    if (FindString(source, "#version 430 core")) return 430;
    if (FindString(source, "#version 420 core")) return 420;
    if (FindString(source, "#version 410 core")) return 410;
    if (FindString(source, "#version 400 core")) return 400;
    if (FindString(source, "#version 330 core")) return 330;
    if (FindString(source, "#version 150 core")) return 150;

    // Check for compatibility profile versions
    if (FindString(source, "#version 460")) return 460;
    if (FindString(source, "#version 450")) return 450;
    if (FindString(source, "#version 440")) return 440;
    if (FindString(source, "#version 430")) return 430;
    if (FindString(source, "#version 420")) return 420;
    if (FindString(source, "#version 410")) return 410;
    if (FindString(source, "#version 400")) return 400;
    if (FindString(source, "#version 330")) return 330;
    if (FindString(source, "#version 150")) return 150;
    if (FindString(source, "#version 140")) return 140;
    if (FindString(source, "#version 130")) return 130;
    if (FindString(source, "#version 120")) return 120;
    if (FindString(source, "#version 110")) return 110;
    if (FindString(source, "#version 100")) return 100;
    return 100;
}
