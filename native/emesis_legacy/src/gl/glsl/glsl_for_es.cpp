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
    std::string result = glslCode;
    {
        /* Strip any existing float/int/sampler/image default precision lines so we emit clean highp defaults once */
        std::istringstream iss(result);
        std::string out;
        out.reserve(result.size() + 640);
        std::string line;
        bool first = true;
        while (std::getline(iss, line)) {
            size_t ws = 0;
            while (ws < line.size() && (line[ws] == ' ' || line[ws] == '\t')) ++ws;
            bool isPrecisionLine = (line.compare(ws, 10, "precision ") == 0) &&
                                   (line.find(';') != std::string::npos);
            if (!isPrecisionLine) {
                if (!first) out += '\n';
                out += line;
                first = false;
            }
        }
        result.swap(out);
    }

    /* SPIRV-Cross emits explicit `uniform mediump sampler2D ...` / `lowp sampler2DShadow ...` on sampler/image uniforms,
       which overrides `precision highp sampler*;` and quantizes 24-bit depth / HDR gbuffers to FP16/FP10 on ARM Mali! */
    if (result.find("sampler") != std::string::npos || result.find("image") != std::string::npos) {
        static const std::regex samplerPrecRx(
            R"(\b(?:lowp|mediump)(\s+(?:[iu]?sampler|[iu]?image)\w*\b))",
            std::regex::ECMAScript | std::regex::optimize);
        result = std::regex_replace(result, samplerPrecRx, "highp$1");
    }

    const int ver = getGLSLVersion(result.c_str());
    std::string precBlock =
        "precision highp float;\n"
        "precision highp int;\n"
        "precision highp sampler2D;\n"
        "precision highp sampler3D;\n"
        "precision highp samplerCube;\n"
        "precision highp sampler2DShadow;\n"
        "precision highp samplerCubeShadow;\n"
        "precision highp sampler2DArray;\n"
        "precision highp sampler2DArrayShadow;\n"
        "precision highp isampler2D;\n"
        "precision highp isampler3D;\n"
        "precision highp isamplerCube;\n"
        "precision highp isampler2DArray;\n"
        "precision highp usampler2D;\n"
        "precision highp usampler3D;\n"
        "precision highp usamplerCube;\n"
        "precision highp usampler2DArray;\n";
    if (ver >= 310) {
        precBlock +=
            "precision highp sampler2DMS;\n"
            "precision highp isampler2DMS;\n"
            "precision highp usampler2DMS;\n"
            "precision highp image2D;\n"
            "precision highp image3D;\n"
            "precision highp imageCube;\n"
            "precision highp image2DArray;\n"
            "precision highp iimage2D;\n"
            "precision highp iimage3D;\n"
            "precision highp iimageCube;\n"
            "precision highp iimage2DArray;\n"
            "precision highp uimage2D;\n"
            "precision highp uimage3D;\n"
            "precision highp uimageCube;\n"
            "precision highp uimage2DArray;\n";
    }
    if (ver >= 320) {
        precBlock +=
            "precision highp samplerBuffer;\n"
            "precision highp isamplerBuffer;\n"
            "precision highp usamplerBuffer;\n"
            "precision highp imageBuffer;\n"
            "precision highp iimageBuffer;\n"
            "precision highp uimageBuffer;\n"
            "precision highp sampler2DMSArray;\n"
            "precision highp isampler2DMSArray;\n"
            "precision highp usampler2DMSArray;\n"
            "precision highp samplerCubeArray;\n"
            "precision highp samplerCubeArrayShadow;\n"
            "precision highp isamplerCubeArray;\n"
            "precision highp usamplerCubeArray;\n"
            "precision highp imageCubeArray;\n"
            "precision highp iimageCubeArray;\n"
            "precision highp uimageCubeArray;\n";
    }

    size_t insertionPos = find_insertion_point(result);
    result.insert(insertionPos, precBlock);
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
        "highp float _emesis_pow(highp float x, highp float y) { return !(x > 0.0) ? ((y <= 0.0) ? 1.0 : 0.0) : pow(x, y); }\n"
        "highp vec2 _emesis_pow(highp vec2 x, highp vec2 y) { return vec2(_emesis_pow(x.x, y.x), _emesis_pow(x.y, y.y)); }\n"
        "highp vec3 _emesis_pow(highp vec3 x, highp vec3 y) { return vec3(_emesis_pow(x.x, y.x), _emesis_pow(x.y, y.y), _emesis_pow(x.z, y.z)); }\n"
        "highp vec4 _emesis_pow(highp vec4 x, highp vec4 y) { return vec4(_emesis_pow(x.x, y.x), _emesis_pow(x.y, y.y), _emesis_pow(x.z, y.z), _emesis_pow(x.w, y.w)); }\n";
    size_t insertPos = find_insertion_point(out);
    out.insert(insertPos, kSafePowDefs);
    return out;
}

static std::string injectSafeClampForMobileGPU(const std::string& glslCode) {
    if (glslCode.find("clamp") == std::string::npos) return glslCode;
    std::string out;
    out.reserve(glslCode.size() + 1536);
    const size_t n = glslCode.size();
    size_t i = 0;
    bool replaced = false;
    while (i < n) {
        size_t pos = glslCode.find("clamp", i);
        if (pos == std::string::npos) {
            out.append(glslCode, i, n - i);
            break;
        }
        bool prev_ok = (pos == 0) ||
                       (!std::isalnum((unsigned char)glslCode[pos - 1]) &&
                        glslCode[pos - 1] != '_' && glslCode[pos - 1] != '.');
        size_t p = pos + 5;
        bool next_ident = (p < n) && (std::isalnum((unsigned char)glslCode[p]) || glslCode[p] == '_');
        size_t lp = p;
        if (prev_ok && !next_ident) {
            while (lp < n && (glslCode[lp] == ' ' || glslCode[lp] == '\t')) ++lp;
        }
        if (prev_ok && !next_ident && lp < n && glslCode[lp] == '(') {
            out.append(glslCode, i, pos - i);
            out += "_emesis_clamp(";
            i = lp + 1;
            replaced = true;
        } else {
            out.append(glslCode, i, p - i);
            i = p;
        }
    }
    if (!replaced) return glslCode;
    static const char* kSafeClampDefs =
        "highp float _emesis_clamp(highp float x, highp float lo, highp float hi) { return isnan(x) ? lo : clamp(x, lo, hi); }\n"
        "highp vec2 _emesis_clamp(highp vec2 x, highp vec2 lo, highp vec2 hi) { return vec2(_emesis_clamp(x.x, lo.x, hi.x), _emesis_clamp(x.y, lo.y, hi.y)); }\n"
        "highp vec3 _emesis_clamp(highp vec3 x, highp vec3 lo, highp vec3 hi) { return vec3(_emesis_clamp(x.x, lo.x, hi.x), _emesis_clamp(x.y, lo.y, hi.y), _emesis_clamp(x.z, lo.z, hi.z)); }\n"
        "highp vec4 _emesis_clamp(highp vec4 x, highp vec4 lo, highp vec4 hi) { return vec4(_emesis_clamp(x.x, lo.x, hi.x), _emesis_clamp(x.y, lo.y, hi.y), _emesis_clamp(x.z, lo.z, hi.z), _emesis_clamp(x.w, lo.w, hi.w)); }\n"
        "highp vec2 _emesis_clamp(highp vec2 x, highp float lo, highp float hi) { return _emesis_clamp(x, vec2(lo), vec2(hi)); }\n"
        "highp vec3 _emesis_clamp(highp vec3 x, highp float lo, highp float hi) { return _emesis_clamp(x, vec3(lo), vec3(hi)); }\n"
        "highp vec4 _emesis_clamp(highp vec4 x, highp float lo, highp float hi) { return _emesis_clamp(x, vec4(lo), vec4(hi)); }\n"
        "highp int _emesis_clamp(highp int x, highp int lo, highp int hi) { return clamp(x, lo, hi); }\n"
        "highp ivec2 _emesis_clamp(highp ivec2 x, highp ivec2 lo, highp ivec2 hi) { return clamp(x, lo, hi); }\n"
        "highp ivec3 _emesis_clamp(highp ivec3 x, highp ivec3 lo, highp ivec3 hi) { return clamp(x, lo, hi); }\n"
        "highp ivec4 _emesis_clamp(highp ivec4 x, highp ivec4 lo, highp ivec4 hi) { return clamp(x, lo, hi); }\n"
        "highp ivec2 _emesis_clamp(highp ivec2 x, highp int lo, highp int hi) { return clamp(x, lo, hi); }\n"
        "highp ivec3 _emesis_clamp(highp ivec3 x, highp int lo, highp int hi) { return clamp(x, lo, hi); }\n"
        "highp ivec4 _emesis_clamp(highp ivec4 x, highp int lo, highp int hi) { return clamp(x, lo, hi); }\n"
        "highp uint _emesis_clamp(highp uint x, highp uint lo, highp uint hi) { return clamp(x, lo, hi); }\n"
        "highp uvec2 _emesis_clamp(highp uvec2 x, highp uvec2 lo, highp uvec2 hi) { return clamp(x, lo, hi); }\n"
        "highp uvec3 _emesis_clamp(highp uvec3 x, highp uvec3 lo, highp uvec3 hi) { return clamp(x, lo, hi); }\n"
        "highp uvec4 _emesis_clamp(highp uvec4 x, highp uvec4 lo, highp uvec4 hi) { return clamp(x, lo, hi); }\n"
        "highp uvec2 _emesis_clamp(highp uvec2 x, highp uint lo, highp uint hi) { return clamp(x, lo, hi); }\n"
        "highp uvec3 _emesis_clamp(highp uvec3 x, highp uint lo, highp uint hi) { return clamp(x, lo, hi); }\n"
        "highp uvec4 _emesis_clamp(highp uvec4 x, highp uint lo, highp uint hi) { return clamp(x, lo, hi); }\n";
    size_t insertPos = find_insertion_point(out);
    out.insert(insertPos, kSafeClampDefs);
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

static void replace_noncall_ident_token(std::string& str, const char* from, const char* to) {
    const size_t flen = std::strlen(from);
    const size_t tlen = std::strlen(to);
    size_t pos = str.find(from);
    if (pos == std::string::npos) return;

    while (pos != std::string::npos) {
        bool prev_ok = (pos == 0) || (!std::isalnum((unsigned char)str[pos - 1]) && str[pos - 1] != '_');
        bool next_ok = (pos + flen >= str.size()) ||
                       (!std::isalnum((unsigned char)str[pos + flen]) && str[pos + flen] != '_');
        if (prev_ok && next_ok) {
            size_t k = pos + flen;
            while (k < str.size() && (str[k] == ' ' || str[k] == '\t')) k++;
            bool is_call = (k < str.size() && str[k] == '(');
            if (!is_call) {
                str.replace(pos, flen, to, tlen);
                pos = str.find(from, pos + tlen);
                continue;
            }
        }
        pos = str.find(from, pos + flen);
    }
}

/* Shim legacy GLSL 110/120/130 & GL_EXT_gpu_shader4 constructs used across < 1.17 OptiFine/ShadersMod packs */
static void shim_legacy_pre117_glsl(std::string& glsl) {
    if (glsl.find("GL_EXT_gpu_shader4") != std::string::npos) {
        static const std::regex extGpu4Rx(R"(^[ \t]*#[ \t]*extension[ \t]+GL_EXT_gpu_shader4[^\r\n]*)",
                                          std::regex::ECMAScript | std::regex::multiline | std::regex::optimize);
        glsl = std::regex_replace(glsl, extGpu4Rx, "// shimmed GL_EXT_gpu_shader4");
    }
    if (glsl.find("noperspective") != std::string::npos) {
        if (glsl.find("GL_NV_shader_noperspective_interpolation") != std::string::npos) {
            static const std::regex extNoPerspRx(
                R"(^[ \t]*#[ \t]*extension[ \t]+GL_NV_shader_noperspective_interpolation[^\r\n]*)",
                std::regex::ECMAScript | std::regex::multiline | std::regex::optimize);
            glsl = std::regex_replace(glsl, extNoPerspRx, "// shimmed GL_NV_shader_noperspective_interpolation");
        }
        replace_ident_token(glsl, "noperspective", " ");
    }
    if (glsl.find("texture_query_lod") != std::string::npos) {
        static const std::regex extQueryLodRx(
            R"(^[ \t]*#[ \t]*extension[ \t]+GL_(?:ARB|EXT)_texture_query_lod[^\r\n]*)",
            std::regex::ECMAScript | std::regex::multiline | std::regex::optimize);
        glsl = std::regex_replace(glsl, extQueryLodRx, "// shimmed texture_query_lod");
    }
    if (glsl.find("texture") != std::string::npos) {
        static const std::regex texVarDeclRx(
            R"(\b(?:sampler\w*|isampler\w*|usampler\w*|vec[234]|ivec[234]|uvec[234]|float|int)\s+texture\b)",
            std::regex::ECMAScript | std::regex::optimize);
        if (std::regex_search(glsl, texVarDeclRx)) {
            replace_noncall_ident_token(glsl, "texture", "vgpu_texture");
        }
    }
    if (glsl.find("sample") != std::string::npos) {
        static const std::regex sampleVarRx(
            R"(\b(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|mat[234]|sampler\w*)\s+sample\b)",
            std::regex::ECMAScript | std::regex::optimize);
        if (std::regex_search(glsl, sampleVarRx)) {
            replace_ident_token(glsl, "sample", "vgpu_Sample");
        }
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
    if (glsl.find("textureQueryLod") == std::string::npos &&
        glsl.find("textureQueryLOD") == std::string::npos) {
        return;
    }
    static const std::regex defRegex(R"(vec2\s+mg_textureQueryLod\s*\()",
                                     std::regex::ECMAScript | std::regex::optimize);
    if (std::regex_search(glsl, defRegex)) return;

    static const char* textureQueryLodImpl =
        "\n#define textureQueryLod mg_textureQueryLod\n"
        "#define textureQueryLOD mg_textureQueryLod\n"
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

static bool has_macro_definition(const std::string& glsl, const char* macro) {
    const size_t mlen = std::strlen(macro);
    size_t pos = glsl.find(macro);
    while (pos != std::string::npos) {
        if (pos + mlen <= glsl.size() &&
            (pos + mlen == glsl.size() || (!std::isalnum((unsigned char)glsl[pos + mlen]) && glsl[pos + mlen] != '_')) &&
            pos > 0 && (!std::isalnum((unsigned char)glsl[pos - 1]) && glsl[pos - 1] != '_')) {
            size_t ls = glsl.rfind('\n', pos);
            ls = (ls == std::string::npos) ? 0 : (ls + 1);
            size_t p = ls;
            while (p < pos && (glsl[p] == ' ' || glsl[p] == '\t')) p++;
            if (p < pos && glsl[p] == '#') {
                p++;
                while (p < pos && (glsl[p] == ' ' || glsl[p] == '\t')) p++;
                if (p + 6 <= pos && glsl.compare(p, 6, "define") == 0 &&
                    (glsl[p + 6] == ' ' || glsl[p + 6] == '\t')) {
                    size_t q = p + 6;
                    while (q < pos && (glsl[q] == ' ' || glsl[q] == '\t')) q++;
                    if (q == pos) return true;
                }
            }
        }
        pos = glsl.find(macro, pos + mlen);
    }
    return false;
}

static void inject_ngg_macro_definition(std::string& glslCode) {
    static const char* macro_definitions =
        "#define NGG_NGG\n"
        "#define NGG_NGG_VERSION " xstr(MAJOR) xstr(MINOR) xstr(REVISION) "\n";
    size_t insertionPos = find_insertion_point(glslCode);
    glslCode.insert(insertionPos, macro_definitions);
    if (glslCode.find("MC_RENDER_STAGE_") != std::string::npos &&
        !has_macro_definition(glslCode, "MC_RENDER_STAGE_NONE")) {
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
    if (glslCode.find("MC_RENDER_QUALITY") != std::string::npos &&
        !has_macro_definition(glslCode, "MC_RENDER_QUALITY")) {
        static const char* rq_fallback =
            "#ifndef MC_RENDER_QUALITY\n"
            "#define MC_RENDER_QUALITY 1.0\n"
            "#endif\n";
        glslCode.insert(find_insertion_point(glslCode), rq_fallback);
    }
    if (glslCode.find("MC_SHADOW_QUALITY") != std::string::npos &&
        !has_macro_definition(glslCode, "MC_SHADOW_QUALITY")) {
        static const char* sq_fallback =
            "#ifndef MC_SHADOW_QUALITY\n"
            "#define MC_SHADOW_QUALITY 1.0\n"
            "#endif\n";
        glslCode.insert(find_insertion_point(glslCode), sq_fallback);
    }
    if (glslCode.find("MC_VERSION") != std::string::npos &&
        !has_macro_definition(glslCode, "MC_VERSION")) {
        static const char* version_fallback =
            "#ifndef MC_VERSION\n"
            "#define MC_VERSION 11605\n"
            "#endif\n";
        glslCode.insert(find_insertion_point(glslCode), version_fallback);
    }
    if (glslCode.find("DH_BLOCK_") != std::string::npos &&
        !has_macro_definition(glslCode, "DH_BLOCK_WATER")) {
        static const char* dh_fallbacks =
            "#ifndef DH_BLOCK_UNKNOWN\n"
            "#define DH_BLOCK_UNKNOWN 0\n"
            "#define DH_BLOCK_LEAVES 1\n"
            "#define DH_BLOCK_STONE 2\n"
            "#define DH_BLOCK_WOOD 3\n"
            "#define DH_BLOCK_METAL 4\n"
            "#define DH_BLOCK_DIRT 5\n"
            "#define DH_BLOCK_LAVA 6\n"
            "#define DH_BLOCK_DEEPSLATE 7\n"
            "#define DH_BLOCK_SNOW 8\n"
            "#define DH_BLOCK_SAND 9\n"
            "#define DH_BLOCK_TERRACOTTA 10\n"
            "#define DH_BLOCK_NETHER_STONE 11\n"
            "#define DH_BLOCK_WATER 12\n"
            "#define DH_BLOCK_GRASS 13\n"
            "#define DH_BLOCK_AIR 14\n"
            "#define DH_BLOCK_ILLUMINATED 15\n"
            "#endif\n";
        glslCode.insert(find_insertion_point(glslCode), dh_fallbacks);
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

static std::string strip_glsl_comments_preserve_lines(const std::string& src) {
    std::string out = src;
    const size_t n = out.size();
    size_t i = 0;
    while (i < n) {
        if (i + 1 < n && out[i] == '/' && out[i + 1] == '/') {
            while (i < n && out[i] != '\n') {
                out[i] = ' ';
                ++i;
            }
        } else if (i + 1 < n && out[i] == '/' && out[i + 1] == '*') {
            out[i] = ' ';
            out[i + 1] = ' ';
            i += 2;
            while (i < n) {
                if (i + 1 < n && out[i] == '*' && out[i + 1] == '/') {
                    out[i] = ' ';
                    out[i + 1] = ' ';
                    i += 2;
                    break;
                }
                if (out[i] != '\n') out[i] = ' ';
                ++i;
            }
        } else {
            ++i;
        }
    }
    return out;
}

static bool is_glsl_decl_type_token(const std::string& tok) {
    return tok == "int" || tok == "uint" || tok == "float" || tok == "double" || tok == "bool" ||
           tok == "vec2" || tok == "vec3" || tok == "vec4" ||
           tok == "ivec2" || tok == "ivec3" || tok == "ivec4" ||
           tok == "uvec2" || tok == "uvec3" || tok == "uvec4" ||
           tok == "bvec2" || tok == "bvec3" || tok == "bvec4" ||
           tok == "mat2" || tok == "mat3" || tok == "mat4" ||
           tok == "highp" || tok == "mediump" || tok == "lowp";
}

static bool needs_implicit_shaderpack_decl(const std::string& clean, const char* ident) {
    const size_t ilen = std::strlen(ident);
    if (clean.find(ident) == std::string::npos) return false;
    if (has_macro_definition(clean, ident)) return false;

    bool referenced = false;
    size_t pos = clean.find(ident);
    while (pos != std::string::npos) {
        bool left_ok = (pos == 0) || (!std::isalnum((unsigned char)clean[pos - 1]) && clean[pos - 1] != '_');
        bool right_ok = (pos + ilen >= clean.size()) ||
                        (!std::isalnum((unsigned char)clean[pos + ilen]) && clean[pos + ilen] != '_');
        if (left_ok && right_ok) {
            size_t ls = clean.rfind('\n', pos);
            ls = (ls == std::string::npos) ? 0 : (ls + 1);
            size_t lp = ls;
            while (lp < pos && (clean[lp] == ' ' || clean[lp] == '\t' || clean[lp] == '\r')) ++lp;
            if (lp < pos && clean[lp] == '#') {
                pos = clean.find(ident, pos + ilen);
                continue;
            }

            size_t prev = pos;
            while (prev > 0 && std::isspace((unsigned char)clean[prev - 1])) --prev;
            if (prev > 0 && clean[prev - 1] == '.') {
                pos = clean.find(ident, pos + ilen);
                continue;
            }

            size_t next = pos + ilen;
            while (next < clean.size() && std::isspace((unsigned char)clean[next])) ++next;
            if (next < clean.size() && clean[next] == '(') {
                return false;
            }

            if (prev > 0 && (std::isalnum((unsigned char)clean[prev - 1]) || clean[prev - 1] == '_')) {
                size_t ts = prev;
                while (ts > 0 && (std::isalnum((unsigned char)clean[ts - 1]) || clean[ts - 1] == '_')) --ts;
                std::string prev_tok = clean.substr(ts, prev - ts);
                if (is_glsl_decl_type_token(prev_tok)) {
                    return false;
                }
            } else if (prev > 0 && clean[prev - 1] == ',') {
                size_t scan = prev - 1;
                int paren_depth = 0;
                while (scan > 0) {
                    char c = clean[scan - 1];
                    if (c == ')') ++paren_depth;
                    else if (c == '(') {
                        if (paren_depth == 0) break;
                        --paren_depth;
                    } else if (paren_depth == 0 && (c == ';' || c == '{' || c == '}')) {
                        break;
                    }
                    --scan;
                }
                while (scan < prev && std::isspace((unsigned char)clean[scan])) ++scan;
                size_t te = scan;
                while (te < prev && (std::isalnum((unsigned char)clean[te]) || clean[te] == '_')) ++te;
                std::string first_tok = clean.substr(scan, te - scan);
                if (first_tok == "uniform" || first_tok == "in" || first_tok == "out" ||
                    first_tok == "attribute" || first_tok == "varying" || first_tok == "const" ||
                    first_tok == "flat" || first_tok == "smooth" || is_glsl_decl_type_token(first_tok)) {
                    return false;
                }
            }

            referenced = true;
        }
        pos = clean.find(ident, pos + ilen);
    }
    return referenced;
}

static void inject_missing_shaderpack_declarations(std::string& glsl, GLenum shaderType) {
    bool maybe_entity = (glsl.find("entityId") != std::string::npos);
    bool maybe_block_entity = (glsl.find("blockEntityId") != std::string::npos);
    bool maybe_item = (glsl.find("currentRenderedItemId") != std::string::npos);
    bool maybe_stage = (glsl.find("renderStage") != std::string::npos);
    bool maybe_dh = (shaderType == GL_VERTEX_SHADER && glsl.find("dhMaterialId") != std::string::npos);
    bool maybe_flat_normals =
        (glsl.find("FlatNormals") != std::string::npos && glsl.find("MC_NORMAL_MAP") != std::string::npos);

    if (!maybe_entity && !maybe_block_entity && !maybe_item && !maybe_stage && !maybe_dh && !maybe_flat_normals) {
        return;
    }

    std::string clean = strip_glsl_comments_preserve_lines(glsl);
    std::string extra;

    if (maybe_entity && needs_implicit_shaderpack_decl(clean, "entityId")) {
        extra += "uniform int entityId;\n";
    }
    if (maybe_block_entity && needs_implicit_shaderpack_decl(clean, "blockEntityId")) {
        extra += "uniform int blockEntityId;\n";
    }
    if (maybe_item && needs_implicit_shaderpack_decl(clean, "currentRenderedItemId")) {
        extra += "uniform int currentRenderedItemId;\n";
    }
    if (maybe_stage && needs_implicit_shaderpack_decl(clean, "renderStage")) {
        extra += "uniform int renderStage;\n";
    }
    if (maybe_dh && needs_implicit_shaderpack_decl(clean, "dhMaterialId")) {
        extra += "in int dhMaterialId;\n";
    }
    if (maybe_flat_normals) {
        static const std::regex flatNormalsVaryingRx(
            R"(\bvarying\s+vec3\s+FlatNormals\s*;)",
            std::regex::ECMAScript | std::regex::optimize);
        std::smatch m;
        if (std::regex_search(clean, m, flatNormalsVaryingRx) &&
            clean.find("vec3 FlatNormals = vec3(0.0);") == std::string::npos) {
            size_t decl_end = (size_t)m.position() + (size_t)m.length();
            if (clean.find("FlatNormals", decl_end) != std::string::npos) {
                size_t endif_pos = clean.find("#endif", decl_end);
                if (endif_pos != std::string::npos) {
                    size_t nl = glsl.find('\n', endif_pos);
                    size_t ins = (nl == std::string::npos) ? glsl.size() : (nl + 1);
                    glsl.insert(ins, "#ifndef MC_NORMAL_MAP\nvec3 FlatNormals = vec3(0.0);\n#endif\n");
                }
            }
        }
    }

    if (!extra.empty()) {
        glsl.insert(find_insertion_point(glsl), extra);
    }
}

static std::string preprocess_glsl(const char* raw_glsl, GLenum shaderType, bool* atomicCounterEmulated,
                                   bool* keepVertexInputLocations) {
    std::string ret(raw_glsl ? raw_glsl : "");
    *keepVertexInputLocations = (shaderType == GL_VERTEX_SHADER) && has_explicit_vertex_input_layout(ret);

    ret = strip_line_directives(ret);
    upgrade_to_compatibility_version(ret);
    shim_legacy_pre117_glsl(ret);

    replace_all(ret, "#ifdef GL_ARB_derivative_control", "#if 0");
    replace_all(ret, "#ifndef GL_ARB_derivative_control", "#if 1");
    replace_all(ret, "mix(, currentDepth, decay2)", "mix(oldDepth, currentDepth, decay2)");
    replace_all(ret, "if (oldDepth != oldDepth)", "if (isnan(oldDepth))");
    replace_all(ret, "NewColor *= OldL / NewL;", "NewColor *= OldL / max(NewL, 1e-6);");
    replace_all(ret, "/get_luma(repcol)", "/max(get_luma(repcol), 1e-6)");
    replace_all(ret, "const mat3 rotInverse = transpose(rot);",
                "const mat3 rotInverse = mat3(rot[0][0], rot[1][0], rot[2][0], rot[0][1], rot[1][1], rot[2][1], "
                "rot[0][2], rot[1][2], rot[2][2]);");

    inject_temporal_filter(ret);
    inject_textureQueryLod(ret);
    inject_ngg_macro_definition(ret);
    inject_missing_shaderpack_declarations(ret, shaderType);

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
        if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STAGE_INPUT, &list, &count) ==
            SPVC_SUCCESS) {
            for (size_t i = 0; i < count; ++i) {
                if (!keep_vertex_in_locations) {
                    spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationLocation);
                }
                spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationNoPerspective);
            }
        }
        if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STAGE_OUTPUT, &list, &count) ==
            SPVC_SUCCESS) {
            for (size_t i = 0; i < count; ++i) {
                spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationLocation);
                spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationNoPerspective);
            }
        }
    } else if (shader_type == GL_FRAGMENT_SHADER) {
        if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STAGE_INPUT, &list, &count) ==
            SPVC_SUCCESS) {
            for (size_t i = 0; i < count; ++i) {
                spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationLocation);
                spvc_compiler_unset_decoration(compiler_glsl, list[i].id, SpvDecorationNoPerspective);
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
    if (glsl_type == GL_FRAGMENT_SHADER &&
        (essl.find("colortex") != std::string::npos || essl.find("gcolor") != std::string::npos ||
         essl.find("gaux") != std::string::npos)) {
        essl = injectSafeClampForMobileGPU(essl);
    }

    return_code = atomicCounterEmulated ? 1 : 0;
    return essl;
}
