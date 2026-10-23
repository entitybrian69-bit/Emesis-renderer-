#include "glsl_for_es.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Include/Types.h>
#include <spirv_cross/spirv_cross_c.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "../../../version.h"

extern "C" {
#include <stdbool.h>
#include "../logs.h"
}

#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

std::string GLSLtoGLSLES(const char* glsl_code, GLenum glsl_type, unsigned int essl_version, unsigned int glsl_version,
                         int& return_code);
std::string GLSLtoGLSLES_2(const char* glsl_code, GLenum glsl_type, unsigned int essl_version, int& return_code);

const char* atomicCounterEmulatedWatermark = "// Non-opaque atomic uniform converted to SSBO";

static TBuiltInResource InitResources() {
    TBuiltInResource Resources{};

    Resources.maxLights = 32;
    Resources.maxClipPlanes = 8;
    Resources.maxTextureUnits = 32;
    Resources.maxTextureCoords = 32;
    Resources.maxVertexAttribs = 64;
    Resources.maxVertexUniformComponents = 4096;
    Resources.maxVaryingFloats = 128;
    Resources.maxVertexTextureImageUnits = 32;
    Resources.maxCombinedTextureImageUnits = 96;
    Resources.maxTextureImageUnits = 32;
    Resources.maxFragmentUniformComponents = 4096;
    Resources.maxDrawBuffers = 32;
    Resources.maxVertexUniformVectors = 1024;
    Resources.maxVaryingVectors = 32;
    Resources.maxFragmentUniformVectors = 1024;
    Resources.maxVertexOutputVectors = 32;
    Resources.maxFragmentInputVectors = 32;
    Resources.minProgramTexelOffset = -8;
    Resources.maxProgramTexelOffset = 7;
    Resources.maxClipDistances = 8;
    Resources.maxComputeWorkGroupCountX = 65535;
    Resources.maxComputeWorkGroupCountY = 65535;
    Resources.maxComputeWorkGroupCountZ = 65535;
    Resources.maxComputeWorkGroupSizeX = 1024;
    Resources.maxComputeWorkGroupSizeY = 1024;
    Resources.maxComputeWorkGroupSizeZ = 64;
    Resources.maxComputeUniformComponents = 2048;
    Resources.maxComputeTextureImageUnits = 32;
    Resources.maxComputeImageUniforms = 16;
    Resources.maxComputeAtomicCounters = 16;
    Resources.maxComputeAtomicCounterBuffers = 4;
    Resources.maxVaryingComponents = 128;
    Resources.maxVertexOutputComponents = 128;
    Resources.maxGeometryInputComponents = 128;
    Resources.maxGeometryOutputComponents = 128;
    Resources.maxFragmentInputComponents = 128;
    Resources.maxImageUnits = 16;
    Resources.maxCombinedImageUnitsAndFragmentOutputs = 32;
    Resources.maxCombinedShaderOutputResources = 32;
    Resources.maxImageSamples = 0;
    Resources.maxVertexImageUniforms = 8;
    Resources.maxTessControlImageUniforms = 0;
    Resources.maxTessEvaluationImageUniforms = 0;
    Resources.maxGeometryImageUniforms = 0;
    Resources.maxFragmentImageUniforms = 16;
    Resources.maxCombinedImageUniforms = 16;
    Resources.maxGeometryTextureImageUnits = 16;
    Resources.maxGeometryOutputVertices = 256;
    Resources.maxGeometryTotalOutputComponents = 1024;
    Resources.maxGeometryUniformComponents = 1024;
    Resources.maxGeometryVaryingComponents = 64;
    Resources.maxTessControlInputComponents = 128;
    Resources.maxTessControlOutputComponents = 128;
    Resources.maxTessControlTextureImageUnits = 16;
    Resources.maxTessControlUniformComponents = 1024;
    Resources.maxTessControlTotalOutputComponents = 4096;
    Resources.maxTessEvaluationInputComponents = 128;
    Resources.maxTessEvaluationOutputComponents = 128;
    Resources.maxTessEvaluationTextureImageUnits = 16;
    Resources.maxTessEvaluationUniformComponents = 1024;
    Resources.maxTessPatchComponents = 120;
    Resources.maxPatchVertices = 32;
    Resources.maxTessGenLevel = 64;
    Resources.maxViewports = 16;
    Resources.maxVertexAtomicCounters = 8;
    Resources.maxTessControlAtomicCounters = 0;
    Resources.maxTessEvaluationAtomicCounters = 0;
    Resources.maxGeometryAtomicCounters = 0;
    Resources.maxFragmentAtomicCounters = 16;
    Resources.maxCombinedAtomicCounters = 16;
    Resources.maxAtomicCounterBindings = 8;
    Resources.maxVertexAtomicCounterBuffers = 2;
    Resources.maxTessControlAtomicCounterBuffers = 0;
    Resources.maxTessEvaluationAtomicCounterBuffers = 0;
    Resources.maxGeometryAtomicCounterBuffers = 0;
    Resources.maxFragmentAtomicCounterBuffers = 4;
    Resources.maxCombinedAtomicCounterBuffers = 8;
    Resources.maxAtomicCounterBufferSize = 16384;
    Resources.maxTransformFeedbackBuffers = 4;
    Resources.maxTransformFeedbackInterleavedComponents = 64;
    Resources.maxCullDistances = 8;
    Resources.maxCombinedClipAndCullDistances = 8;
    Resources.maxSamples = 4;
    Resources.maxMeshOutputVerticesNV = 256;
    Resources.maxMeshOutputPrimitivesNV = 512;
    Resources.maxMeshWorkGroupSizeX_NV = 32;
    Resources.maxMeshWorkGroupSizeY_NV = 1;
    Resources.maxMeshWorkGroupSizeZ_NV = 1;
    Resources.maxTaskWorkGroupSizeX_NV = 32;
    Resources.maxTaskWorkGroupSizeY_NV = 1;
    Resources.maxTaskWorkGroupSizeZ_NV = 1;
    Resources.maxMeshViewCountNV = 4;

    Resources.limits.nonInductiveForLoops = true;
    Resources.limits.whileLoops = true;
    Resources.limits.doWhileLoops = true;
    Resources.limits.generalUniformIndexing = true;
    Resources.limits.generalAttributeMatrixVectorIndexing = true;
    Resources.limits.generalVaryingIndexing = true;
    Resources.limits.generalSamplerIndexing = true;
    Resources.limits.generalVariableIndexing = true;
    Resources.limits.generalConstantMatrixVectorIndexing = true;

    return Resources;
}

static const TBuiltInResource& GetDefaultResources() {
    static const TBuiltInResource kResources = InitResources();
    return kResources;
}

int getGLSLVersion(const char* glsl_code) {
    if (!glsl_code) return -1;
    const char* p = glsl_code;
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
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '#') {
            const char* h = p + 1;
            while (*h == ' ' || *h == '\t') h++;
            if (strncmp(h, "version", 7) == 0 && std::isspace((unsigned char)h[7])) {
                h += 7;
                while (*h == ' ' || *h == '\t') h++;
                if (std::isdigit((unsigned char)h[0]) &&
                    std::isdigit((unsigned char)h[1]) &&
                    std::isdigit((unsigned char)h[2])) {
                    return (h[0] - '0') * 100 + (h[1] - '0') * 10 + (h[2] - '0');
                }
                return -1;
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return -1;
}

/* Scan past `#version` AND top-level `#extension` directives (never stopping inside an `#if` block). */
static size_t find_insertion_point(const std::string& glsl) {
    const size_t len = glsl.size();
    size_t p = 0;
    size_t last_directive_end = 0;
    int if_depth = 0;
    bool ext_in_if = false;

    auto is_ident = [](char c) {
        return std::isalnum((unsigned char)c) || c == '_';
    };

    while (p < len) {
        while (p < len && (glsl[p] == ' ' || glsl[p] == '\t' || glsl[p] == '\r')) p++;
        if (p + 1 < len && glsl[p] == '/' && glsl[p + 1] == '/') {
            while (p < len && glsl[p] != '\n') p++;
            if (p < len) p++;
            continue;
        }
        if (p + 1 < len && glsl[p] == '/' && glsl[p + 1] == '*') {
            p += 2;
            while (p + 1 < len && !(glsl[p] == '*' && glsl[p + 1] == '/')) p++;
            p = (p + 1 < len) ? (p + 2) : len;
            continue;
        }
        if (p < len && glsl[p] == '\n') {
            p++;
            continue;
        }
        if (p >= len) break;

        if (glsl[p] == '#') {
            size_t h = p + 1;
            while (h < len && (glsl[h] == ' ' || glsl[h] == '\t')) h++;
            size_t nl = glsl.find('\n', h);
            size_t line_end = (nl == std::string::npos) ? len : (nl + 1);

            if ((h + 5 <= len && glsl.compare(h, 5, "ifdef") == 0 && (h + 5 == len || !is_ident(glsl[h + 5]))) ||
                (h + 6 <= len && glsl.compare(h, 6, "ifndef") == 0 && (h + 6 == len || !is_ident(glsl[h + 6]))) ||
                (h + 2 <= len && glsl.compare(h, 2, "if") == 0 && (h + 2 == len || !is_ident(glsl[h + 2])))) {
                if_depth++;
            } else if (h + 5 <= len && glsl.compare(h, 5, "endif") == 0 && (h + 5 == len || !is_ident(glsl[h + 5]))) {
                if (if_depth > 0) if_depth--;
                if (if_depth == 0 && ext_in_if) {
                    last_directive_end = line_end;
                    ext_in_if = false;
                }
            } else {
                bool is_ver = (h + 7 <= len && glsl.compare(h, 7, "version") == 0 &&
                               (h + 7 == len || !is_ident(glsl[h + 7])));
                bool is_ext = (h + 9 <= len && glsl.compare(h, 9, "extension") == 0 &&
                               (h + 9 == len || !is_ident(glsl[h + 9])));
                if (is_ver || is_ext) {
                    if (if_depth == 0) {
                        last_directive_end = line_end;
                    } else {
                        ext_in_if = true;
                    }
                }
            }
            p = line_end;
            continue;
        } else if (if_depth == 0) {
            break;
        }

        while (p < len && glsl[p] != '\n') {
            if (p + 1 < len && glsl[p] == '/' && glsl[p + 1] == '*') {
                p += 2;
                while (p + 1 < len && !(glsl[p] == '*' && glsl[p + 1] == '/')) p++;
                p = (p + 1 < len) ? (p + 2) : len;
                continue;
            }
            p++;
        }
        if (p < len) p++;
    }
    return last_directive_end;
}

static size_t find_after_version_line(const std::string& glsl) {
    const size_t len = glsl.size();
    size_t p = 0;
    while (p < len) {
        if (p + 1 < len && glsl[p] == '/' && glsl[p + 1] == '*') {
            p += 2;
            while (p + 1 < len && !(glsl[p] == '*' && glsl[p + 1] == '/')) p++;
            p = (p + 1 < len) ? (p + 2) : len;
            continue;
        }
        while (p < len && (glsl[p] == ' ' || glsl[p] == '\t' || glsl[p] == '\r')) p++;
        if (p < len && glsl[p] == '#') {
            size_t h = p + 1;
            while (h < len && (glsl[h] == ' ' || glsl[h] == '\t')) h++;
            if (h + 7 <= len && glsl.compare(h, 7, "version") == 0 &&
                (h + 7 == len || std::isspace((unsigned char)glsl[h + 7]))) {
                size_t nl = glsl.find('\n', h);
                return (nl == std::string::npos) ? len : (nl + 1);
            }
        }
        while (p < len && glsl[p] != '\n') p++;
        if (p < len) p++;
    }
    return 0;
}

std::string forceSupporterOutput(const std::string& glslCode) {
    bool hasPrecisionFloat =
        glslCode.find("precision ") != std::string::npos && glslCode.find("float;") != std::string::npos;
    bool hasPrecisionInt =
        glslCode.find("precision ") != std::string::npos && glslCode.find("int;") != std::string::npos;

    std::string result = glslCode;
    std::string precisionFloat;
    std::string precisionInt;

    if (hasPrecisionFloat && hasPrecisionInt) {
        std::istringstream iss(result);
        std::string out;
        out.reserve(result.size() + 64);
        std::string line;
        bool first = true;
        while (std::getline(iss, line)) {
            bool isPrecisionLine = (line.find("precision ") != std::string::npos) &&
                                   (line.find("float;") != std::string::npos || line.find("int;") != std::string::npos);
            if (!isPrecisionLine) {
                if (!first) out += '\n';
                out += line;
                first = false;
            }
        }
        result.swap(out);
        precisionFloat = "precision highp float;\n";
        precisionInt = "precision highp int;\n";
    } else {
        precisionFloat = hasPrecisionFloat ? "" : "precision highp float;\n";
        precisionInt = hasPrecisionInt ? "" : "precision highp int;\n";
    }

    size_t insertionPos = find_insertion_point(result);
    result.insert(insertionPos, precisionFloat + precisionInt);
    return result;
}

std::string removeLayoutBinding(const std::string& glslCode) {
    if (glslCode.find("binding") == std::string::npos) return glslCode;
    std::string out;
    out.reserve(glslCode.size());
    const size_t n = glslCode.size();
    size_t i = 0;
    while (i < n) {
        size_t pos = glslCode.find("layout", i);
        if (pos == std::string::npos) {
            out.append(glslCode, i, n - i);
            break;
        }
        bool word_prev = (pos == 0) || (!std::isalnum((unsigned char)glslCode[pos - 1]) && glslCode[pos - 1] != '_');
        size_t p = pos + 6;
        bool word_next = (p >= n) || (!std::isalnum((unsigned char)glslCode[p]) && glslCode[p] != '_');
        if (!word_prev || !word_next) {
            out.append(glslCode, i, p - i);
            i = p;
            continue;
        }
        size_t ws = p;
        while (ws < n && (glslCode[ws] == ' ' || glslCode[ws] == '\t')) ++ws;
        if (ws >= n || glslCode[ws] != '(') {
            out.append(glslCode, i, ws - i);
            i = ws;
            continue;
        }
        size_t close = glslCode.find(')', ws + 1);
        if (close == std::string::npos) {
            out.append(glslCode, i, n - i);
            break;
        }
        /* Parse comma-separated qualifiers inside layout(...) and drop `binding = <int>` */
        out.append(glslCode, i, pos - i);
        std::string kept;
        bool removed_any = false;
        size_t cur = ws + 1;
        while (cur <= close) {
            size_t comma = glslCode.find(',', cur);
            if (comma == std::string::npos || comma > close) comma = close;
            size_t s = cur, e = comma;
            while (s < e && std::isspace((unsigned char)glslCode[s])) ++s;
            while (e > s && std::isspace((unsigned char)glslCode[e - 1])) --e;
            bool is_binding = false;
            if (e - s >= 7 && glslCode.compare(s, 7, "binding") == 0) {
                size_t k = s + 7;
                while (k < e && std::isspace((unsigned char)glslCode[k])) ++k;
                if (k < e && glslCode[k] == '=') {
                    ++k;
                    while (k < e && std::isspace((unsigned char)glslCode[k])) ++k;
                    size_t d0 = k;
                    while (k < e && std::isdigit((unsigned char)glslCode[k])) ++k;
                    if (k > d0 && k == e) is_binding = true;
                }
            }
            if (is_binding) {
                removed_any = true;
            } else if (e > s) {
                if (!kept.empty()) kept += ", ";
                kept.append(glslCode, s, e - s);
            }
            cur = comma + 1;
        }
        if (!removed_any) {
            out.append(glslCode, pos, close + 1 - pos);
            i = close + 1;
        } else if (!kept.empty()) {
            out += "layout(";
            out += kept;
            out += ")";
            i = close + 1;
        } else {
            size_t after = close + 1;
            while (after < n && (glslCode[after] == ' ' || glslCode[after] == '\t')) ++after;
            i = after;
        }
    }
    return out;
}

static std::string injectSafePowForMobileGPU(const std::string& glslCode) {
    if (glslCode.find("pow") == std::string::npos) return glslCode;
    std::string out;
    out.reserve(glslCode.size() + 384);
    const size_t n = glslCode.size();
    size_t i = 0;
    bool replaced = false;
    while (i < n) {
        size_t pos = glslCode.find("pow", i);
        if (pos == std::string::npos) {
            out.append(glslCode, i, n - i);
            break;
        }
        bool prev_ok = (pos == 0) ||
                       (!std::isalnum((unsigned char)glslCode[pos - 1]) &&
                        glslCode[pos - 1] != '_' && glslCode[pos - 1] != '.');
        size_t p = pos + 3;
        bool next_ident = (p < n) && (std::isalnum((unsigned char)glslCode[p]) || glslCode[p] == '_');
        size_t lp = p;
        if (prev_ok && !next_ident) {
            while (lp < n && (glslCode[lp] == ' ' || glslCode[lp] == '\t')) ++lp;
        }
        if (prev_ok && !next_ident && lp < n && glslCode[lp] == '(') {
            out.append(glslCode, i, pos - i);
            out += "_emesis_pow(";
            i = lp + 1;
            replaced = true;
        } else {
            out.append(glslCode, i, p - i);
            i = p;
        }
    }
    if (!replaced) return glslCode;
    static const char* kSafePowDefs =
        "highp float _emesis_pow(highp float x, highp float y) { return pow(max(x, 1e-8), y); }\n"
        "highp vec2 _emesis_pow(highp vec2 x, highp vec2 y) { return pow(max(x, vec2(1e-8)), y); }\n"
        "highp vec3 _emesis_pow(highp vec3 x, highp vec3 y) { return pow(max(x, vec3(1e-8)), y); }\n"
        "highp vec4 _emesis_pow(highp vec4 x, highp vec4 y) { return pow(max(x, vec4(1e-8)), y); }\n";
    size_t insertPos = find_insertion_point(out);
    out.insert(insertPos, kSafePowDefs);
    return out;
}

std::string processOutColorLocations(const std::string& glslCode) {
    if (glslCode.find("outColor") == std::string::npos) return glslCode;
    static const std::regex pattern(R"(\n(out highp vec4 outColor)(\d+);)",
                                    std::regex::ECMAScript | std::regex::optimize);
    return std::regex_replace(glslCode, pattern, "\nlayout(location=$2) $1$2;");
}

bool checkIfAtomicCounterBufferEmulated(const std::string& glslCode) {
    return glslCode.find(atomicCounterEmulatedWatermark) != std::string::npos;
}

char* GLSLtoGLSLES_c(const char* glsl_code, GLenum glsl_type, unsigned int essl_version, unsigned int glsl_version,
                     int* return_code) {
    int tmp_return_code = -1;
    std::string result = GLSLtoGLSLES(glsl_code, glsl_type, essl_version, glsl_version, tmp_return_code);
    if (return_code) *return_code = tmp_return_code;
    if (tmp_return_code < 0 || result.empty()) {
        return nullptr;
    }

    char* cstr = (char*)malloc(result.size() + 1);
    if (!cstr) {
        if (return_code) *return_code = -1;
        return nullptr;
    }
    memcpy(cstr, result.c_str(), result.size() + 1);
    return cstr;
}

std::string GLSLtoGLSLES(const char* glsl_code, GLenum glsl_type, unsigned int essl_version, unsigned int glsl_version,
                         int& return_code) {
    (void)glsl_version;
    return_code = -1;
    std::string converted = GLSLtoGLSLES_2(glsl_code, glsl_type, essl_version, return_code);
    return (return_code >= 0) ? converted : std::string();
}

static std::string strip_line_directives(const std::string& glslCode) {
    if (glslCode.find("#line") == std::string::npos) return glslCode;
    std::string result;
    result.reserve(glslCode.size());
    size_t length = glslCode.size();
    size_t start = 0;
    size_t current = 0;

    while (current < length) {
        size_t lineStart = current;
        while (current < length && (glslCode[current] == ' ' || glslCode[current] == '\t')) {
            current++;
        }
        bool isLineDirective = (current + 5 <= length && glslCode.compare(current, 5, "#line") == 0);
        while (current < length && glslCode[current] != '\r' && glslCode[current] != '\n') {
            current++;
        }
        size_t newlineLength = 0;
        if (current < length) {
            newlineLength = (glslCode[current] == '\r' && current + 1 < length && glslCode[current + 1] == '\n') ? 2 : 1;
        }
        if (isLineDirective) {
            if (lineStart > start) result.append(glslCode, start, lineStart - start);
            current += newlineLength;
            start = current;
        } else {
            current += newlineLength;
        }
    }
    if (current > start) result.append(glslCode, start, current - start);
    return result;
}

static inline void replace_all(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos) return;
    do {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    } while ((start_pos = str.find(from, start_pos)) != std::string::npos);
}

static void replace_ident_token(std::string& str, const char* from, const char* to) {
    const size_t flen = std::strlen(from);
    const size_t tlen = std::strlen(to);
    size_t pos = str.find(from);
    if (pos == std::string::npos) return;

    while (pos != std::string::npos) {
        bool prev_ok = (pos == 0) || (!std::isalnum((unsigned char)str[pos - 1]) && str[pos - 1] != '_');
        bool next_ok = (pos + flen >= str.size()) ||
                       (!std::isalnum((unsigned char)str[pos + flen]) && str[pos + flen] != '_');
        if (prev_ok && next_ok) {
            str.replace(pos, flen, to, tlen);
            pos = str.find(from, pos + tlen);
        } else {
            pos = str.find(from, pos + flen);
        }
    }
}

/* Shim legacy GLSL 110/120/130 & GL_EXT_gpu_shader4 constructs used across < 1.17 OptiFine/ShadersMod packs */
static void shim_legacy_pre117_glsl(std::string& glsl) {
    if (glsl.find("GL_EXT_gpu_shader4") != std::string::npos) {
        static const std::regex extGpu4Rx(R"(^[ \t]*#[ \t]*extension[ \t]+GL_EXT_gpu_shader4[^\r\n]*)",
                                          std::regex::ECMAScript | std::regex::multiline | std::regex::optimize);
        glsl = std::regex_replace(glsl, extGpu4Rx, "// shimmed GL_EXT_gpu_shader4");
    }
    if (glsl.find("unsigned") != std::string::npos) {
        static const std::regex unsignedIntRx(R"(\bunsigned\s+int\b)",
                                              std::regex::ECMAScript | std::regex::optimize);
        glsl = std::regex_replace(glsl, unsignedIntRx, "uint");
    }
    if (glsl.find("texelFetch") != std::string::npos) {
        replace_ident_token(glsl, "texelFetch1D", "texelFetch");
        replace_ident_token(glsl, "texelFetch2D", "texelFetch");
        replace_ident_token(glsl, "texelFetch3D", "texelFetch");
        replace_ident_token(glsl, "texelFetch1DOffset", "texelFetchOffset");
        replace_ident_token(glsl, "texelFetch2DOffset", "texelFetchOffset");
        replace_ident_token(glsl, "texelFetch3DOffset", "texelFetchOffset");
    }
    if (glsl.find("textureSize") != std::string::npos) {
        replace_ident_token(glsl, "textureSize1D", "textureSize");
        replace_ident_token(glsl, "textureSize2D", "textureSize");
        replace_ident_token(glsl, "textureSize3D", "textureSize");
        replace_ident_token(glsl, "textureSizeCube", "textureSize");
    }
    if (glsl.find("GradARB") != std::string::npos) {
        replace_ident_token(glsl, "texture1DGradARB", "textureGrad");
        replace_ident_token(glsl, "texture2DGradARB", "textureGrad");
        replace_ident_token(glsl, "texture3DGradARB", "textureGrad");
        replace_ident_token(glsl, "textureCubeGradARB", "textureGrad");
        replace_ident_token(glsl, "texture2DProjGradARB", "textureProjGrad");
    }
}

bool process_non_opaque_atomic_to_ssbo(std::string& source) {
    if (source.find("atomic_uint") == std::string::npos && source.find("atomicCounter") == std::string::npos) {
        return false;
    }

    std::set<std::string> atomic_vars;
    static const std::regex decl_rx(
        R"(layout\s*\(\s*binding\s*=\s*(\d+)\s*(?:,\s*offset\s*=\s*(\d+)\s*)?\)\s*uniform\s+atomic_uint\s+(\w+)\s*;)",
        std::regex::icase | std::regex::optimize);

    std::smatch m;
    auto it = source.cbegin();
    while (std::regex_search(it, source.cend(), m, decl_rx)) {
        size_t prefix = std::distance(source.cbegin(), it);
        size_t match_pos = prefix + m.position(0);
        size_t match_len = m.length(0);

        std::string binding = m[1].str();
        std::string var = m[3].str();
        atomic_vars.insert(var);

        std::string repl = "layout(std430, binding=" + binding + ") buffer AtomicCounterSSBO_" + binding +
                           " {\n    uint " + var + ";\n};\n";
        source.replace(match_pos, match_len, repl);
        it = source.cbegin() + match_pos + repl.size();
    }

    if (atomic_vars.empty()) return false;

    for (const auto& var : atomic_vars) {
        source = std::regex_replace(
            source, std::regex(R"(\batomicCounterIncrement\s*\(\s*)" + var + R"(\s*\))", std::regex::icase),
            "atomicAdd(" + var + ", 1u)");
        source = std::regex_replace(
            source, std::regex(R"(\batomicCounterDecrement\s*\(\s*)" + var + R"(\s*\))", std::regex::icase),
            "atomicAdd(" + var + ", uint(-1))");
        source = std::regex_replace(
            source, std::regex(R"(\batomicCounterAdd\s*\(\s*)" + var + R"(\s*,\s*([^)]+)\s*\))", std::regex::icase),
            "atomicAdd(" + var + ", $1)");
        source = std::regex_replace(
            source, std::regex(R"(\batomicCounter\s*\(\s*)" + var + R"(\s*\))", std::regex::icase), var);
    }

    static const std::regex rx_barrier(R"(([ \t]*\batomicAdd\b[^;]*;))",
                                       std::regex::icase | std::regex::optimize);
    std::string result;
    size_t last_pos = 0;
    for (auto bit = std::sregex_iterator(source.begin(), source.end(), rx_barrier);
         bit != std::sregex_iterator(); ++bit) {
        size_t start_pos = bit->position();
        size_t end_pos = start_pos + bit->length();
        result.append(source, last_pos, start_pos - last_pos);
        result += bit->str();
        result += "\n    memoryBarrierBuffer();";
        last_pos = end_pos;
    }
    result.append(source, last_pos, source.size() - last_pos);
    source.swap(result);

    source += "\n";
    source += atomicCounterEmulatedWatermark;
    return true;
}

static void inject_textureQueryLod(std::string& glsl) {
    if (glsl.find("textureQueryLod") == std::string::npos) return;
    static const std::regex defRegex(R"(vec2\s+mg_textureQueryLod\s*\()",
                                     std::regex::ECMAScript | std::regex::optimize);
    if (std::regex_search(glsl, defRegex)) return;

    static const char* textureQueryLodImpl =
        "\n#define textureQueryLod mg_textureQueryLod\n"
        "vec2 mg_textureQueryLod(sampler2D tex, vec2 uv) {\n"
        "    vec2 texSizeF = vec2(textureSize(tex, 0));\n"
        "    vec2 dFdx_uv = dFdx(uv * texSizeF);\n"
        "    vec2 dFdy_uv = dFdy(uv * texSizeF);\n"
        "    float maxDerivative = max(length(dFdx_uv), length(dFdy_uv));\n"
        "    return vec2(log2(max(maxDerivative, 1e-8)));\n"
        "}\n";

    size_t insertPos = find_insertion_point(glsl);
    glsl.insert(insertPos, textureQueryLodImpl);
}

static inline void inject_temporal_filter(std::string& glsl) {
    if (glsl.find("GI_TemporalFilter") == std::string::npos) return;
    static const std::regex defRegex(R"(vec4\s+GI_TemporalFilter\s*\()",
                                     std::regex::ECMAScript | std::regex::optimize);
    if (std::regex_search(glsl, defRegex)) return;

    static const std::regex uniformRegex(
        R"(^\s*(?:layout\s*\([^)]*\)\s*)?uniform\s+\w+(?:\s*\[\s*\d+\s*\])?\s+\w+(?:\s*\[\s*\d+\s*\])?\s*;.*$)",
        std::regex::ECMAScript | std::regex::multiline | std::regex::optimize);
    size_t insertPos = find_insertion_point(glsl);
    for (auto it = std::sregex_iterator(glsl.begin(), glsl.end(), uniformRegex);
         it != std::sregex_iterator(); ++it) {
        insertPos = it->position() + it->length();
    }

    static const char* GI_TemporalFilterImpl = R"(
vec4 GI_TemporalFilter() {
    vec2 uv = gl_FragCoord.xy / screenSize;
    uv += taaJitter * pixelSize;
    vec4 currentGI = texture(colortex0, uv);
    float depth = texture(depthtex0, uv).r;
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = gbufferProjectionInverse * clipPos;
    viewPos /= viewPos.w;
    vec4 worldPos = gbufferModelViewInverse * viewPos;
    vec4 prevClipPos = gbufferPreviousProjection * (gbufferPreviousModelView * worldPos);
    prevClipPos /= prevClipPos.w;
    vec2 prevUV = prevClipPos.xy * 0.5 + 0.5;
    vec4 historyGI = texture(colortex1, prevUV);
    float difference = length(currentGI.rgb - historyGI.rgb);
    float thresholdValue = 0.1;
    float adaptiveBlend = mix(0.9, 0.0, smoothstep(thresholdValue, thresholdValue * 2.0, difference));
    vec4 filteredGI = mix(currentGI, historyGI, adaptiveBlend);
    if (difference > thresholdValue * 2.0) {
        filteredGI = currentGI;
    }
    return filteredGI;
}
)";
    glsl.insert(insertPos, GI_TemporalFilterImpl);
}

#define xstr(s) str(s)
#define str(s) #s

static void inject_ngg_macro_definition(std::string& glslCode) {
    static const char* macro_definitions =
        "#define NGG_NGG\n"
        "#define NGG_NGG_VERSION " xstr(MAJOR) xstr(MINOR) xstr(REVISION) "\n";
    size_t insertionPos = find_insertion_point(glslCode);
    glslCode.insert(insertionPos, macro_definitions);
    if (glslCode.find("MC_RENDER_STAGE_") != std::string::npos) {
        static const char* stage_fallbacks =
            "#ifndef MC_RENDER_STAGE_NONE\n"
            "#define MC_RENDER_STAGE_NONE 0\n"
            "#define MC_RENDER_STAGE_SKY 1\n"
            "#define MC_RENDER_STAGE_SUNSET 2\n"
            "#define MC_RENDER_STAGE_CUSTOM_SKY 3\n"
            "#define MC_RENDER_STAGE_SUN 4\n"
            "#define MC_RENDER_STAGE_MOON 5\n"
            "#define MC_RENDER_STAGE_STARS 6\n"
            "#define MC_RENDER_STAGE_VOID 7\n"
            "#define MC_RENDER_STAGE_TERRAIN_SOLID 8\n"
            "#define MC_RENDER_STAGE_TERRAIN_CUTOUT_MIPPED 9\n"
            "#define MC_RENDER_STAGE_TERRAIN_CUTOUT 10\n"
            "#define MC_RENDER_STAGE_ENTITIES 11\n"
            "#define MC_RENDER_STAGE_BLOCK_ENTITIES 12\n"
            "#define MC_RENDER_STAGE_DESTROY 13\n"
            "#define MC_RENDER_STAGE_OUTLINE 14\n"
            "#define MC_RENDER_STAGE_DEBUG 15\n"
            "#define MC_RENDER_STAGE_HAND_SOLID 16\n"
            "#define MC_RENDER_STAGE_TERRAIN_TRANSLUCENT 17\n"
            "#define MC_RENDER_STAGE_TRIPWIRE 18\n"
            "#define MC_RENDER_STAGE_PARTICLES 19\n"
            "#define MC_RENDER_STAGE_CLOUDS 20\n"
            "#define MC_RENDER_STAGE_RAIN_SNOW 21\n"
            "#define MC_RENDER_STAGE_WORLD_BORDER 22\n"
            "#define MC_RENDER_STAGE_HAND_TRANSLUCENT 23\n"
            "#endif\n";
        glslCode.insert(find_insertion_point(glslCode), stage_fallbacks);
    }
}

static void upgrade_to_compatibility_version(std::string& glsl) {
    const size_t len = glsl.size();
    size_t p = 0;
    while (p < len) {
        if (p + 1 < len && glsl[p] == '/' && glsl[p + 1] == '*') {
            p += 2;
            while (p + 1 < len && !(glsl[p] == '*' && glsl[p + 1] == '/')) p++;
            p = (p + 1 < len) ? (p + 2) : len;
            continue;
        }
        size_t line_start = p;
        while (p < len && (glsl[p] == ' ' || glsl[p] == '\t' || glsl[p] == '\r')) p++;
        if (p < len && glsl[p] == '#') {
            size_t h = p + 1;
            while (h < len && (glsl[h] == ' ' || glsl[h] == '\t')) h++;
            if (h + 7 <= len && glsl.compare(h, 7, "version") == 0 &&
                (h + 7 == len || std::isspace((unsigned char)glsl[h + 7]))) {
                size_t nl = glsl.find('\n', h);
                size_t line_end = (nl == std::string::npos) ? len : (nl + 1);
                glsl.replace(line_start, line_end - line_start, "#version 460 compatibility\n");
                break;
            }
        }
        while (p < len && glsl[p] != '\n') p++;
        if (p < len) p++;
    }
    if (getGLSLVersion(glsl.c_str()) == -1) {
        glsl.insert(0, "#version 460 compatibility\n");
    }
    if ((glsl.find("Lod") != std::string::npos || glsl.find("Grad") != std::string::npos) &&
        glsl.find("GL_ARB_shader_texture_lod") == std::string::npos) {
        size_t after_ver = find_after_version_line(glsl);
        glsl.insert(after_ver, "#extension GL_ARB_shader_texture_lod : enable\n");
    }
}

static bool has_explicit_vertex_input_layout(const std::string& glsl) {
    if (glsl.find("layout") == std::string::npos) return false;
    static const std::regex explicitInRx(
        R"(layout\s*\([^)]*\blocation\s*=\s*\d+[^)]*\)\s*(?:highp\s+|mediump\s+|lowp\s+)?(?:in|attribute)\b)",
        std::regex::ECMAScript | std::regex::optimize);
    return std::regex_search(glsl, explicitInRx);
}

static std::string preprocess_glsl(const char* raw_glsl, GLenum shaderType, bool* atomicCounterEmulated,
                                   bool* keepVertexInputLocations) {
    (void)shaderType;
    std::string ret(raw_glsl ? raw_glsl : "");
    *keepVertexInputLocations = (shaderType == GL_VERTEX_SHADER) && has_explicit_vertex_input_layout(ret);

    ret = strip_line_directives(ret);
    upgrade_to_compatibility_version(ret);
    shim_legacy_pre117_glsl(ret);

    replace_all(ret, "#ifdef GL_ARB_derivative_control", "#if 0");
    replace_all(ret, "#ifndef GL_ARB_derivative_control", "#if 1");
    replace_all(ret, "mix(, currentDepth, decay2)", "mix(oldDepth, currentDepth, decay2)");
    replace_all(ret, "if (oldDepth != oldDepth)", "if (isnan(oldDepth))");
    replace_all(ret, "const mat3 rotInverse = transpose(rot);",
                "const mat3 rotInverse = mat3(rot[0][0], rot[1][0], rot[2][0], rot[0][1], rot[1][1], rot[2][1], "
                "rot[0][2], rot[1][2], rot[2][2]);");

    inject_temporal_filter(ret);
    inject_textureQueryLod(ret);
    inject_ngg_macro_definition(ret);

    *atomicCounterEmulated = process_non_opaque_atomic_to_ssbo(ret);
    return ret;
}

static bool has_real_glslang_error(const char* log) {
    if (!log || !log[0]) return false;
    const char* p = log;
    while ((p = std::strstr(p, "ERROR:")) != nullptr) {
        if (std::strncmp(p, "ERROR: #version: compilation for SPIR-V does not support the compatibility profile", 82) !=
            0) {
            return true;
        }
        p += 6;
    }
    return false;
}

static std::vector<unsigned int> glsl_to_spirv(GLenum shader_type, const char* const* shader_src, int& errc) {
    EShLanguage shader_language;
    switch (shader_type) {
    case GL_VERTEX_SHADER:
        shader_language = EShLanguage::EShLangVertex;
        break;
    case GL_FRAGMENT_SHADER:
        shader_language = EShLanguage::EShLangFragment;
        break;
    case GL_COMPUTE_SHADER:
        shader_language = EShLanguage::EShLangCompute;
        break;
    case GL_TESS_CONTROL_SHADER:
        shader_language = EShLanguage::EShLangTessControl;
        break;
    case GL_TESS_EVALUATION_SHADER:
        shader_language = EShLanguage::EShLangTessEvaluation;
        break;
    case GL_GEOMETRY_SHADER:
        shader_language = EShLanguage::EShLangGeometry;
        break;
    default:
        errc = -1;
        return {};
    }

    glslang::TShader shader(shader_language);
    shader.setStrings(shader_src, 1);

    using namespace glslang;
    shader.setEnvInput(EShSourceGlsl, shader_language, EShClientOpenGL, 460);
    shader.setEnvClient(EShClientOpenGL, EShTargetOpenGL_450);
    shader.setEnvTarget(EShTargetSpv, EShTargetSpv_1_0);
    shader.setAutoMapLocations(true);
    shader.setAutoMapBindings(true);

    const TBuiltInResource& res = GetDefaultResources();
    const EShMessages msgs = (EShMessages)(EShMsgDefault | EShMsgRelaxedErrors);

    if (!shader.parse(&res, 460, ECompatibilityProfile, false, false, msgs)) {
        DBG(SHUT_LOGD("glslang: GLSL Compiling ERROR:\n%s", shader.getInfoLog());)
        errc = -1;
        return {};
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(msgs) || !program.getIntermediate(shader_language) ||
        has_real_glslang_error(shader.getInfoLog()) || has_real_glslang_error(program.getInfoLog())) {
        DBG(SHUT_LOGD("glslang: GLSL Linking ERROR:\n%s\n%s", shader.getInfoLog(), program.getInfoLog());)
        errc = -1;
        return {};
    }

    std::vector<unsigned int> spirv_code;
    glslang::SpvOptions spvOptions;
    spvOptions.disableOptimizer = false;
    glslang::GlslangToSpv(*program.getIntermediate(shader_language), spirv_code, &spvOptions);
    errc = 0;
    return spirv_code;
}

static std::string spirv_to_essl(const std::vector<unsigned int>& spirv, GLenum shader_type,
                                 unsigned int essl_version, bool keep_vertex_in_locations, int& errc) {
    spvc_context context = nullptr;
    spvc_parsed_ir ir = nullptr;
    spvc_compiler compiler_glsl = nullptr;
    spvc_compiler_options options = nullptr;
    spvc_resources resources = nullptr;
    const char* result = nullptr;

    if (spvc_context_create(&context) != SPVC_SUCCESS || !context) {
        errc = -1;
        return "";
    }

    if (spvc_context_parse_spirv(context, spirv.data(), spirv.size(), &ir) != SPVC_SUCCESS ||
        spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,
                                     &compiler_glsl) != SPVC_SUCCESS ||
        spvc_compiler_create_shader_resources(compiler_glsl, &resources) != SPVC_SUCCESS) {
        spvc_context_destroy(context);
        errc = -1;
        return "";
    }

    /* Strip auto-assigned locations on inter-stage varyings (so vertex & fragment match by name
       across ESSL 300/310/320 regardless of declaration order) and on legacy vertex attributes
       (so glBindAttribLocation for _gl4es_*, mc_Entity, mc_midTexCoord, at_tangent takes effect).
       Preserve fragment stage_outputs locations so MRT FragData[0..N] binds to exact drawbuffers. */
    const spvc_reflected_resource* list = nullptr;
    size_t count = 0;
    bool drop_dead_vgpu_fragcolor = false;
    if (shader_type == GL_VERTEX_SHADER) {
        if (!keep_vertex_in_locations &&
            spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STAGE_INPUT, &list, &count) ==
                SPVC_SUCCESS) {
            for (size_t i = 0; i < count; ++i) {
                spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationLocation);
            }
        }
        if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STAGE_OUTPUT, &list, &count) ==
            SPVC_SUCCESS) {
            for (size_t i = 0; i < count; ++i) {
                spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationLocation);
            }
        }
    } else if (shader_type == GL_FRAGMENT_SHADER) {
        if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STAGE_INPUT, &list, &count) ==
            SPVC_SUCCESS) {
            for (size_t i = 0; i < count; ++i) {
                spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationLocation);
            }
        }
        spvc_set active_vars = nullptr;
        spvc_resources active_resources = nullptr;
        const spvc_reflected_resource* active_out_list = nullptr;
        size_t active_out_count = 0;
        if (spvc_compiler_get_active_interface_variables(compiler_glsl, &active_vars) == SPVC_SUCCESS && active_vars &&
            spvc_compiler_create_shader_resources_for_active_variables(compiler_glsl, &active_resources, active_vars) ==
                SPVC_SUCCESS &&
            active_resources) {
            spvc_resources_get_resource_list_for_type(active_resources, SPVC_RESOURCE_TYPE_STAGE_OUTPUT,
                                                      &active_out_list, &active_out_count);
        }
        if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STAGE_OUTPUT, &list, &count) ==
                SPVC_SUCCESS &&
            count > 0) {
            bool has_other_active_out = false;
            bool vgpu_fragcolor_inactive = false;
            for (size_t i = 0; i < count; ++i) {
                const char* nm = spvc_compiler_get_name(compiler_glsl, list[i].id);
                if (!nm || !nm[0]) nm = list[i].name;
                bool is_active = (active_resources == nullptr);
                for (size_t a = 0; a < active_out_count; ++a) {
                    if (active_out_list[a].id == list[i].id) {
                        is_active = true;
                        break;
                    }
                }
                if (nm && std::strcmp(nm, "vgpu_FragColor") == 0) {
                    if (!is_active && count > 1) vgpu_fragcolor_inactive = true;
                } else if (is_active) {
                    has_other_active_out = true;
                }
            }
            drop_dead_vgpu_fragcolor = has_other_active_out && vgpu_fragcolor_inactive;

            uint64_t used_locs = 0;
            for (size_t i = 0; i < count; ++i) {
                const char* nm = spvc_compiler_get_name(compiler_glsl, list[i].id);
                if (!nm || !nm[0]) nm = list[i].name;
                if (drop_dead_vgpu_fragcolor && nm && std::strcmp(nm, "vgpu_FragColor") == 0) {
                    spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationLocation);
                    continue;
                }
                if (spvc_compiler_has_decoration(compiler_glsl, list[i].id, SpvDecorationLocation)) {
                    unsigned loc = spvc_compiler_get_decoration(compiler_glsl, list[i].id, SpvDecorationLocation);
                    if (loc < 64 && !(used_locs & (1ULL << loc))) {
                        used_locs |= (1ULL << loc);
                    } else {
                        spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationLocation);
                    }
                }
            }
            for (size_t i = 0; i < count; ++i) {
                if (!spvc_compiler_has_decoration(compiler_glsl, list[i].id, SpvDecorationLocation)) {
                    unsigned loc = 0;
                    const char* nm = spvc_compiler_get_name(compiler_glsl, list[i].id);
                    if (!nm || !nm[0]) nm = list[i].name;
                    if (nm) {
                        if (std::strncmp(nm, "outColor", 8) == 0 && std::isdigit((unsigned char)nm[8])) {
                            loc = (unsigned)std::atoi(nm + 8);
                        } else if (std::strncmp(nm, "vgpu_FragData", 13) == 0 && std::isdigit((unsigned char)nm[13])) {
                            loc = (unsigned)std::atoi(nm + 13);
                        }
                    }
                    while (loc < 64 && (used_locs & (1ULL << loc))) ++loc;
                    spvc_compiler_set_decoration(compiler_glsl, list[i].id, SpvDecorationLocation, loc);
                    if (loc < 64) used_locs |= (1ULL << loc);
                }
            }
        }
    }

    unsigned int target_es = 300;
    if (essl_version >= 320) {
        target_es = 320;
    } else if (essl_version >= 310) {
        target_es = 310;
    }

    spvc_compiler_create_compiler_options(compiler_glsl, &options);
    spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_GLSL_VERSION, target_es);
    spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_ES, SPVC_TRUE);
    spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_FLOAT_PRECISION_HIGHP, SPVC_TRUE);
    spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_INT_PRECISION_HIGHP, SPVC_TRUE);
    spvc_compiler_install_compiler_options(compiler_glsl, options);

    if (spvc_compiler_compile(compiler_glsl, &result) != SPVC_SUCCESS || !result) {
        spvc_context_destroy(context);
        errc = -1;
        return "";
    }

    std::string essl(result);
    spvc_context_destroy(context);
    if (drop_dead_vgpu_fragcolor) {
        static const std::regex deadFragColorRx(
            R"(^[ \t]*(?:layout\s*\([^)]*\)\s*)?out\s+(?:highp\s+|mediump\s+|lowp\s+)?vec4\s+vgpu_FragColor\s*;\r?\n?)",
            std::regex::ECMAScript | std::regex::multiline | std::regex::optimize);
        essl = std::regex_replace(essl, deadFragColorRx, "");
    }
    errc = 0;
    return essl;
}

std::string GLSLtoGLSLES_2(const char* glsl_code, GLenum glsl_type, unsigned int essl_version, int& return_code) {
    static std::once_flag s_glslang_once;
    std::call_once(s_glslang_once, []() { glslang::InitializeProcess(); });

    bool atomicCounterEmulated = false;
    bool keepVertexInLocs = false;
    std::string correct_glsl_str = preprocess_glsl(glsl_code, glsl_type, &atomicCounterEmulated, &keepVertexInLocs);

    const char* s[] = {correct_glsl_str.c_str()};
    int errc = 0;
    std::vector<unsigned int> spirv_code = glsl_to_spirv(glsl_type, s, errc);
    if (errc != 0 || spirv_code.empty()) {
        return_code = -1;
        return "";
    }

    std::string essl = spirv_to_essl(spirv_code, glsl_type, essl_version, keepVertexInLocs, errc);
    if (errc != 0 || essl.empty()) {
        return_code = -2;
        return "";
    }

    if (glsl_type != GL_COMPUTE_SHADER) {
        essl = removeLayoutBinding(essl);
    }
    essl = processOutColorLocations(essl);
    essl = forceSupporterOutput(essl);
    essl = injectSafePowForMobileGPU(essl);

    return_code = atomicCounterEmulated ? 1 : 0;
    return essl;
}
