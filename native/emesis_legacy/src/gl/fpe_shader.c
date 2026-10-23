#include "fpe_shader.h"

#include <stdio.h>

#include "string_utils.h"
#include "init.h"
#include "../glx/hardext.h"
#include "logs.h"

// #define DEBUG
#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

const char* fpeshader_signature = "// FPE_Shader generated\n";

static char* shad = NULL;
static int shad_cap = 0;

static int comments = 1;

#define ShadAppend(S) shad = Append(shad, &shad_cap, S)

const char* texnoproj[] = {"texture2D", "texture2D", "texture2D", "textureCube",
                           "textureStreamIMG"}; // textureRectange and 3D are emulated with 2D

const char* texvecsize[] = {"vec2", "vec2", "vec2", "vec3", "vec2"};
const char* texxyzsize[] = {"xy", "xy", "xy", "xyz", "xy"};
//                          2D          Rectangle    3D             CubeMap      Stream
const char* texname[] = {"texture2D", "texture2D", "texture2D", "textureCube",
                         "textureStreamIMG"}; // textureRectange and 3D are emulated with 2D

const char* texsampler[] = {"sampler2D", "sampler2D", "sampler2D", "samplerCube", "samplerStreamIMG"};
int texnsize[] = {2, 2, 3, 3, 2};
const char texcoordname[] = {'s', 't', 'r', 'q'};
const char texcoordNAME[] = {'S', 'T', 'R', 'Q'};
const char texcoordxy[] = {'x', 'y', 'z', 'w'};

const char* gl4es_alphaRefSource = "uniform float _gl4es_AlphaRef;\n";

const char* fpe_texenvSrc(int src, int tmu, int twosided) {
    static char buff[200];
    switch (src) {
    case FPE_SRC_TEXTURE:
        sprintf(buff, "texColor%d", tmu);
        break;
    case FPE_SRC_TEXTURE0:
    case FPE_SRC_TEXTURE1:
    case FPE_SRC_TEXTURE2:
    case FPE_SRC_TEXTURE3:
    case FPE_SRC_TEXTURE4:
    case FPE_SRC_TEXTURE5:
    case FPE_SRC_TEXTURE6:
    case FPE_SRC_TEXTURE7:
    case FPE_SRC_TEXTURE8:
    case FPE_SRC_TEXTURE9:
    case FPE_SRC_TEXTURE10:
    case FPE_SRC_TEXTURE11:
    case FPE_SRC_TEXTURE12:
    case FPE_SRC_TEXTURE13:
    case FPE_SRC_TEXTURE14:
    case FPE_SRC_TEXTURE15:
        sprintf(buff, "texColor%d", src - FPE_SRC_TEXTURE0); // should check if texture is enabled
        break;
    case FPE_SRC_CONSTANT:
        sprintf(buff, "_gl4es_TextureEnvColor_%d", tmu);
        break;
    case FPE_SRC_PRIMARY_COLOR:
        sprintf(buff, "%s", twosided ? "((gl_FrontFacing)?Color:BackColor)" : "Color");
        break;
    case FPE_SRC_PREVIOUS:
        sprintf(buff, "fColor");
        break;
    case FPE_SRC_ONE:
        sprintf(buff, "vec4(1.0)");
        break;
    case FPE_SRC_ZERO:
        sprintf(buff, "vec4(0.0)");
        break;
    case FPE_SRC_SECONDARY_COLOR:
        sprintf(buff, "%s", twosided ? "((gl_FrontFacing)?SecColor:SecBackColor)" : "SecColor");
        break;
    }
    return buff;
}

int fpe_texenvSecondary(fpe_state_t* state) {
    // check if one of the texenv need secondary color...
    for (int i = 0; i < hardext.maxtex; i++) {
        int t = state->texture[i].textype;
        if (t) {
            int texenv = state->texenv[i].texenv;
            if (texenv == FPE_COMBINE) {
                int combine_rgb = state->texcombine[i] & 0xf;
                int src_r[4];
                src_r[0] = state->texenv[i].texsrcrgb0;
                src_r[1] = state->texenv[i].texsrcrgb1;
                src_r[2] = state->texenv[i].texsrcrgb2;
                src_r[3] = state->texenv[i].texsrcrgb3;
                if (combine_rgb == FPE_CR_DOT3_RGBA) {
                    src_r[2] = -1;
                } else {
                    if (combine_rgb == FPE_CR_REPLACE) {
                        src_r[1] = src_r[2] = -1;
                    } else if (combine_rgb >= FPE_CR_MOD_ADD) {
                        // need all 3
                    } else if (combine_rgb != FPE_CR_INTERPOLATE) {
                        src_r[2] = -1;
                    }
                }
                if (src_r[0] == FPE_SRC_SECONDARY_COLOR || src_r[1] == FPE_SRC_SECONDARY_COLOR ||
                    src_r[2] == FPE_SRC_SECONDARY_COLOR)
                    return 1;
            }
        }
    }
    return 0;
}

char* fpe_packed64(uint64_t x, int s, int k) {
    static char buff[8][65];
    static int idx = 0;

    idx &= 7;
    uint64_t mask = (1L << k) - 1L;

    const char* hex = "0123456789ABCDEF";

    int j = s / k;
    buff[idx][j] = '\0';
    for (int i = 0; i < s; i += k) {
        buff[idx][--j] = hex[(x & mask)];
        x >>= k;
    }
    return buff[idx++];
}
char* fpe_packed(int x, int s, int k) {
    static char buff[8][33];
    static int idx = 0;

    idx &= 7;
    int mask = (1 << k) - 1;

    const char* hex = "0123456789ABCDEF";

    int j = s / k;
    buff[idx][j] = '\0';
    for (int i = 0; i < s; i += k) {
        buff[idx][--j] = hex[(x & mask)];
        x >>= k;
    }
    return buff[idx++];
}
char* fpe_binary(int x, int s) {
    return fpe_packed(x, s, 1);
}

const char* const* fpe_VertexShader(shaderconv_need_t* need, fpe_state_t* state) {
    // vertex is first called, so 1st time init is only here
    if (!shad_cap) shad_cap = 1024;
    if (!shad) shad = (char*)malloc(shad_cap);
    // state can be NULL, so provide a 0 default
    fpe_state_t default_state = {0};
    int is_default = !!need;
    if (!state) state = &default_state;
    int lighting = state->lighting;
    int twosided = state->twosided && lighting;
    if (need && ((need->need_color > 1) || (need->need_secondary > 1))) twosided = 1;
    int light_separate = state->light_separate && lighting;
    int secondary = (state->colorsum && !(lighting && light_separate)) || fpe_texenvSecondary(state) ||
                    (need && need->need_secondary);
    int fog = state->fog || (need && need->need_fogcoord);
    int fogsource = state->fogsource;
    int fogdist = state->fogdist;
    int fogmode = state->fogmode;
    int color_material = state->color_material && lighting;
    int point = state->point;
    int pointsprite = state->pointsprite;
    int headers = 0;
    int planes = state->plane;
    char buff[1024];
    int need_vertex = 0;
    int need_eyeplane[MAX_TEX][4] = {0};
    int need_objplane[MAX_TEX][4] = {0};
    int need_adjust[MAX_TEX] = {0};
    int need_lightproduct[2][MAX_LIGHT] = {0};
    int cm_front_nullexp = state->cm_front_nullexp;
    int cm_back_nullexp = state->cm_back_nullexp;
    int texgens = 0;
    int texmats = 0;
    const char* fogp = hardext.highp ? "highp" : "mediump";

    for (int i = 0; i < hardext.maxtex; ++i) {
        if (state->texgen[i].texgen_s || state->texgen[i].texgen_t || state->texgen[i].texgen_r ||
            state->texgen[i].texgen_q)
            texgens = 1;
        if (state->texture[i].texmat) texmats = 1;
    }

    strcpy(shad, fpeshader_signature);

    comments = globals4es.comments;
    DBG(comments = 1 - comments;) // When DEBUG is activated, the effect of LIBGL_COMMENTS is reversed

    if (comments) {
        sprintf(buff,
                "// ** Vertex Shader **\n// ligthting=%d (twosided=%d, separate=%d, color_material=%d)\n// "
                "secondary=%d, planes=%s\n// point=%d%s\n",
                lighting, twosided, light_separate, color_material, secondary, fpe_binary(planes, 6), point,
                need ? " with need" : "");
        ShadAppend(buff);
        headers += CountLine(buff);
        if (need) {
            sprintf(buff, "// need: color=%d, texs=%s, fogcoord=%d\n", need->need_color,
                    fpe_binary(need->need_texs, 16), need->need_fogcoord);
            ShadAppend(buff);
            headers += CountLine(buff);
        }
    }
    if (!is_default) {
        ShadAppend("varying vec4 Color;\n"); // might be unused...
        headers++;
    }
    if (planes) {
        for (int i = 0; i < hardext.maxplanes; i++) {
            if ((planes >> i) & 1) {
                sprintf(buff, "uniform highp vec4 _gl4es_ClipPlane_%d;\n", i);
                ShadAppend(buff);
                ++headers;
                sprintf(buff, "varying mediump float clippedvertex_%d;\n", i);
                ShadAppend(buff);
                ++headers;
            }
        }
    }
    if (lighting) {
        sprintf(buff,
                "struct _gl4es_FPELightSourceParameters1\n"
                "{\n"
                "%s"
                "   highp vec4 specular;\n"
                "   highp vec4 position;\n"
                "   highp vec3 spotDirection;\n"
                "   highp float spotExponent;\n"
                "   highp float spotCosCutoff;\n"
                "   highp float constantAttenuation;\n"
                "   highp float linearAttenuation;\n"
                "   highp float quadraticAttenuation;\n"
                "};\n",
                (color_material) ? "   highp vec4 ambient;\n"
                                   "   highp vec4 diffuse;\n"
                                 : "");
        ShadAppend(buff);
        headers += CountLine(buff);
        sprintf(buff,
                "struct _gl4es_FPELightSourceParameters0\n"
                "{\n"
                "%s"
                "   highp vec4 specular;\n"
                "   highp vec4 position;\n"
                "   highp vec3 spotDirection;\n"
                "   highp float spotExponent;\n"
                "   highp float spotCosCutoff;\n"
                "};\n",
                (color_material) ? "   highp vec4 ambient;\n"
                                   "   highp vec4 diffuse;\n"
                                 : "");
        ShadAppend(buff);
        headers += CountLine(buff);

        sprintf(buff, "struct _gl4es_LightProducts\n"
                      "{\n"
                      "   highp vec4 ambient;\n"
                      "   highp vec4 diffuse;\n"
                      "   highp vec4 specular;\n"
                      "};\n");
        ShadAppend(buff);
        headers += CountLine(buff);

        if (!(cm_front_nullexp && color_material)) {
            ShadAppend("uniform highp float _gl4es_FrontMaterial_shininess;\n");
            headers++;
        }
        if (twosided && !(cm_back_nullexp && color_material)) {
            ShadAppend("uniform highp float _gl4es_BackMaterial_shininess;\n");
            headers++;
        }
        if (!(color_material &&
              (state->cm_front_mode == FPE_CM_DIFFUSE || state->cm_front_mode == FPE_CM_AMBIENTDIFFUSE))) {
            ShadAppend("uniform highp float _gl4es_FrontMaterial_alpha;\n");
            headers++;
            if (twosided) {
                ShadAppend("uniform highp float _gl4es_BackMaterial_alpha;\n");
                headers++;
            }
        }
        for (int i = 0; i < hardext.maxlights; i++) {
            if (state->light & (1 << i)) {
                sprintf(buff, "uniform _gl4es_FPELightSourceParameters%d _gl4es_LightSource_%d;\n",
                        (state->light_direction >> i & 1) ? 1 : 0, i);
                ShadAppend(buff);
                headers++;

                sprintf(buff, "uniform _gl4es_LightProducts _gl4es_FrontLightProduct_%d;\n", i);
                ShadAppend(buff);
                headers++;

                if (twosided) {
                    sprintf(buff, "uniform _gl4es_LightProducts _gl4es_BackLightProduct_%d;\n", i);
                    ShadAppend(buff);
                    headers++;
                }
            }
        }
    }
    if (!is_default) {
        if (twosided) {
            ShadAppend("varying vec4 BackColor;\n");
            headers++;
        }
        if (light_separate || secondary) {
            ShadAppend("varying vec4 SecColor;\n");
            headers++;
            if (twosided) {
                ShadAppend("varying vec4 SecBackColor;\n");
                headers++;
            }
        }
    }
    if (fog) {
#if 0 // vertex fog
        ShadAppend("varying mediump float FogF;\n");
        headers++;
        if(fogsource==FPE_FOG_SRC_DEPTH && need_vertex<1)
            need_vertex = 1;
#else // pixel fog
        if (fogsource == FPE_FOG_SRC_COORD)
            sprintf(buff, "varying %s float FogSrc;\n", fogp);
        else {
            if (need_vertex < 1) need_vertex = 1;
            switch (fogdist) {
            case FPE_FOG_DIST_RADIAL:
                sprintf(buff, "varying %s vec3 FogSrc;\n", fogp);
                break;
            case FPE_FOG_DIST_PLANE:
                sprintf(buff, "varying %s float FogSrc;\n", fogp);
                break;
            default:
                sprintf(buff, "varying %s float FogSrc;\n", fogp);
            }
        }
        ShadAppend(buff);
#endif
    }
    // textures coordinates
    for (int i = 0; i < hardext.maxtex; i++) {
        int t = state->texture[i].textype;
        if (need) t = (need->need_texs & (1 << i)) ? 1 : 0;
        if (t) {
            sprintf(buff, "varying %s _gl4es_TexCoord_%d;\n", texvecsize[t - 1], i);
            ShadAppend(buff);
            headers++;
            if (state->texture[i].texmat) {
                sprintf(buff, "uniform highp mat4 _gl4es_TextureMatrix_%d;\n", i);
                ShadAppend(buff);
                headers++;
            }
        }
    }
    // let's start
    ShadAppend("\nvoid main() {\n");
    int need_normal = 0;
    int normal_line = CountLine(shad) - headers;
    if (planes) {
        for (int i = 0; i < hardext.maxplanes; i++) {
            if ((planes >> i) & 1) {
                sprintf(buff, "clippedvertex_%d = dot(vertex, _gl4es_ClipPlane_%d);\n", i, i);
                ShadAppend(buff);
            }
        }
        if (!need_vertex) need_vertex = 1;
        // ShadAppend("gl_Position = clipvertex;\n");
    }
    ShadAppend("gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n");
    // initial Color / lighting calculation
    if (!lighting) {
        if (is_default && need) {
            if (need->need_color >= 1) ShadAppend("gl_FrontColor = gl_Color;\n");
            if (need->need_color == 2) ShadAppend("gl_BackColor = gl_Color;\n");
            if (need->need_secondary >= 1) ShadAppend("gl_FrontSecondaryColor = gl_SecondaryColor;\n");
            if (need->need_secondary == 2) ShadAppend("gl_BackSecondaryColor = gl_SecondaryColor;\n");
        } else {
            if (!need || (need && need->need_color)) ShadAppend("Color = gl_Color;\n");
            if (secondary) {
                ShadAppend("SecColor = gl_SecondaryColor;\n");
            }
        }
    } else {
        if (comments) {
            sprintf(buff, "// ColorMaterial On/Off=%d Front = %d Back = %d\n", color_material, state->cm_front_mode,
                    state->cm_back_mode);
            ShadAppend(buff);
        }
        if (is_default && need) {
            ShadAppend("vec4 Color;\n");
            if (twosided) ShadAppend("vec4 BackColor\n");
            if (secondary) ShadAppend("vec4 SecColor\n;");
            if (secondary && twosided) ShadAppend("vec4 SecBackColor\n");
        }
        // material emission
        char fm_emission[60], fm_ambient[60], fm_diffuse[60], fm_specular[60];
        char bm_emission[60], bm_ambient[60], bm_diffuse[60], bm_specular[60];
        sprintf(fm_emission, "%s",
                (color_material && state->cm_front_mode == FPE_CM_EMISSION) ? "gl_Color" : "gl_FrontMaterial.emission");
        sprintf(fm_ambient, "%s",
                (color_material &&
                 (state->cm_front_mode == FPE_CM_AMBIENT || state->cm_front_mode == FPE_CM_AMBIENTDIFFUSE))
                    ? "gl_Color"
                    : "gl_FrontMaterial.ambient");
        sprintf(fm_diffuse, "%s",
                (color_material &&
                 (state->cm_front_mode == FPE_CM_DIFFUSE || state->cm_front_mode == FPE_CM_AMBIENTDIFFUSE))
                    ? "gl_Color.xyz * _gl4es_LightSource_"
                    : "_gl4es_FrontLightProduct_");
        sprintf(fm_specular, "%s",
                (color_material && state->cm_front_mode == FPE_CM_SPECULAR) ? "gl_Color.xyz * _gl4es_LightSource_"
                                                                            : "_gl4es_FrontLightProduct_");
        if (twosided) {
            sprintf(bm_emission, "%s",
                    (color_material && state->cm_back_mode == FPE_CM_EMISSION) ? "gl_Color"
                                                                               : "gl_BackMaterial.emission");
            sprintf(bm_ambient, "%s",
                    (color_material &&
                     (state->cm_back_mode == FPE_CM_AMBIENT || state->cm_back_mode == FPE_CM_AMBIENTDIFFUSE))
                        ? "gl_Color"
                        : "gl_BackMaterial.ambient");
            sprintf(bm_diffuse, "%s",
                    (color_material &&
                     (state->cm_back_mode == FPE_CM_DIFFUSE || state->cm_back_mode == FPE_CM_AMBIENTDIFFUSE))
                        ? "gl_Color.xyz * _gl4es_LightSource_"
                        : "_gl4es_BackLightProduct_");
            sprintf(bm_specular, "%s",
                    (color_material && state->cm_back_mode == FPE_CM_SPECULAR) ? "gl_Color.xyz * _gl4es_LightSource_"
                                                                               : "_gl4es_BackLightProduct_");
        }

        if (color_material &&
            (state->cm_front_mode == FPE_CM_EMISSION || state->cm_front_mode == FPE_CM_AMBIENT ||
             state->cm_front_mode == FPE_CM_AMBIENTDIFFUSE ||
             (twosided && (state->cm_back_mode == FPE_CM_EMISSION || state->cm_back_mode == FPE_CM_AMBIENT ||
                           state->cm_back_mode == FPE_CM_AMBIENTDIFFUSE)))) {
            sprintf(buff, "Color = %s;\n", fm_emission);
            ShadAppend(buff);
            if (twosided) {
                sprintf(buff, "BackColor = %s;\n", bm_emission);
                ShadAppend(buff);
            }
            sprintf(buff, "Color += %s*gl_LightModel.ambient;\n", fm_ambient);
            ShadAppend(buff);
            if (twosided) {
                sprintf(buff, "BackColor += %s*gl_LightModel.ambient;\n", bm_ambient);
                ShadAppend(buff);
            }
        } else {
            ShadAppend("Color = gl_FrontLightModelProduct.sceneColor;\n");
            if (twosided) {
                ShadAppend("BackColor = gl_BackLightModelProduct.sceneColor;\n");
            }
        }
        if (light_separate) {
            ShadAppend("SecColor=vec4(0.);\n");
            if (twosided) ShadAppend("SecBackColor=vec4(0.);\n");
        }
        ShadAppend("highp float att;\n");
        ShadAppend("highp float spot;\n");
        ShadAppend("highp vec3 VP;\n");
        ShadAppend("highp float lVP;\n");
        ShadAppend("highp float nVP;\n");
        ShadAppend("highp vec3 aa,dd,ss;\n");
        ShadAppend("highp vec3 hi;\n");
        if (twosided) ShadAppend("highp vec3 back_aa,back_dd,back_ss;\n");
        need_normal = 1;
        for (int i = 0; i < hardext.maxlights; i++) {
            if (state->light & (1 << i)) {
                if (comments) {
                    sprintf(buff, "// light %d on, light_direction=%d, light_cutoff180=%d\n", i,
                            (state->light_direction >> i & 1), (state->light_cutoff180 >> i & 1));
                    ShadAppend(buff);
                }
                // enabled light i
                // att depend on light position w
                if ((state->light_direction >> i & 1) == 0) { // flag is 1 if light is has w!=0
                    ShadAppend("att = 1.0;\n");
                    sprintf(buff, "VP = normalize(_gl4es_LightSource_%d.position.xyz);\n", i);
                    ShadAppend(buff);
                } else {
                    sprintf(buff, "VP = _gl4es_LightSource_%d.position.xyz - vertex.xyz;\n", i);
                    ShadAppend(buff);
                    ShadAppend("lVP = length(VP);\n");
                    sprintf(buff,
                            "att = 1.0/(_gl4es_LightSource_%d.constantAttenuation + "
                            "lVP*(_gl4es_LightSource_%d.linearAttenuation + _gl4es_LightSource_%d.quadraticAttenuation "
                            "* lVP));\n",
                            i, i, i);
                    ShadAppend(buff);
                    ShadAppend("VP = normalize(VP);\n");
                    if (!need_vertex) need_vertex = 1;
                }
                // spot depend on spotlight cutoff angle
                if ((state->light_cutoff180 >> i & 1) == 0) {
                    // ShadAppend("spot = 1.0;\n");
                } else {
                    /*if((state->light_direction>>i&1)==0) {
                        sprintf(buff, "spot = max(dot(-normalize(vertex.xyz), _gl4es_LightSource_%d.spotDirection),
                    0.);\n", i); if(!need_vertex) need_vertex=1; } else*/
                    { sprintf(buff, "spot = max(dot(-VP, _gl4es_LightSource_%d.spotDirection), 0.);\n", i); }
                    ShadAppend(buff);
                    sprintf(buff,
                            "if(spot<_gl4es_LightSource_%d.spotCosCutoff) spot=0.0; else spot=pow(spot, "
                            "_gl4es_LightSource_%d.spotExponent);\n",
                            i, i);
                    ShadAppend(buff);
                    ShadAppend("att *= spot;\n");
                }
                if (color_material &&
                    (state->cm_front_mode == FPE_CM_AMBIENT || state->cm_front_mode == FPE_CM_AMBIENTDIFFUSE)) {
                    sprintf(buff, "aa = %s.xyz * _gl4es_LightSource_%d.ambient.xyz;\n", fm_ambient, i);
                    ShadAppend(buff);
                } else {
                    sprintf(buff, "aa = _gl4es_FrontLightProduct_%d.ambient.xyz;\n", i);
                    ShadAppend(buff);
                    need_lightproduct[0][i] = 1;
                }
                if (twosided) {
                    if (color_material &&
                        (state->cm_back_mode == FPE_CM_AMBIENT || state->cm_back_mode == FPE_CM_AMBIENTDIFFUSE)) {
                        sprintf(buff, "back_aa = %s.xyz * _gl4es_LightSource_%d.ambient.xyz;\n", bm_ambient, i);
                        ShadAppend(buff);
                    } else {
                        sprintf(buff, "back_aa = _gl4es_BackLightProduct_%d.ambient.xyz;\n", i);
                        ShadAppend(buff);
                        need_lightproduct[1][i] = 1;
                    }
                }
                sprintf(buff, "nVP = dot(normal, VP);\n");
                ShadAppend(buff);
                sprintf(buff, "dd = (nVP>0.)?(nVP * %s%d.diffuse.xyz):vec3(0.);\n", fm_diffuse, i);
                ShadAppend(buff);
                need_lightproduct[0][i] = 1;
                if (twosided) {
                    sprintf(buff, "back_dd = (nVP<0.)?(-nVP * %s%d.diffuse.xyz):vec3(0.);\n", bm_diffuse, i);
                    ShadAppend(buff);
                    need_lightproduct[1][i] = 1;
                }
                if (state->light_localviewer) {
                    ShadAppend("hi = normalize(VP + normalize(-vertex.xyz));\n");
                    if (!need_vertex) need_vertex = 1;
                } else {
                    ShadAppend("hi = normalize(VP + vec3(0., 0., 1.));\n");
                }
                ShadAppend("lVP = dot(normal, hi);\n");
                if (cm_front_nullexp)
                    sprintf(buff, "ss = (nVP>0. && lVP>0.)?(pow(lVP, %s)*%s%d.specular.xyz):vec3(0.);\n",
                            (color_material) ? "gl_FrontMaterial.shininess" : "_gl4es_FrontMaterial_shininess",
                            fm_specular, i);
                else
                    sprintf(buff, "ss = (nVP>0. && lVP>0.)?(%s%d.specular.xyz):vec3(0.);\n", fm_specular, i);
                ShadAppend(buff);
                if (twosided) {
                    if (state->cm_back_nullexp) // 1, exp is not null
                        sprintf(buff, "back_ss = (nVP<0. && lVP<0.)?(pow(-lVP, %s)*%s%d.specular.xyz):vec3(0.);\n",
                                (color_material) ? "gl_BackMaterial.shininess" : "_gl4es_BackMaterial_shininess",
                                bm_specular, i);
                    else
                        sprintf(buff, "back_ss = (nVP<0. && lVP<0.)?(%s%d.specular.xyz):vec3(0.);\n", bm_specular, i);
                    ShadAppend(buff);
                }
                if (state->light_separate) {
                    ShadAppend("Color.rgb += att*(aa+dd);\n");
                    ShadAppend("SecColor.rgb += att*(ss);\n");
                    if (twosided) {
                        ShadAppend("BackColor.rgb += att*(back_aa+back_dd);\n");
                        ShadAppend("SecBackColor.rgb += att*(back_ss);\n");
                    }
                } else {
                    ShadAppend("Color.rgb += att*(aa+dd+ss);\n");
                    if (twosided) ShadAppend("BackColor.rgb += att*(back_aa+back_dd+back_ss);\n");
                }
                if (comments) {
                    sprintf(buff, "// end of light %d\n", i);
                    ShadAppend(buff);
                }
            }
        }
        sprintf(buff, "Color.a = %s;\n",
                (color_material &&
                 (state->cm_front_mode == FPE_CM_DIFFUSE || state->cm_front_mode == FPE_CM_AMBIENTDIFFUSE))
                    ? "gl_Color.a"
                    : "_gl4es_FrontMaterial_alpha");
        ShadAppend(buff);
        ShadAppend("Color.rgb = clamp(Color.rgb, 0., 1.);\n");
        if (twosided) {
            sprintf(buff, "BackColor.a = %s;\n",
                    (color_material &&
                     (state->cm_back_mode == FPE_CM_DIFFUSE || state->cm_back_mode == FPE_CM_AMBIENTDIFFUSE))
                        ? "gl_Color.a"
                        : "_gl4es_BackMaterial_alpha");
            ShadAppend("BackColor.rgb = clamp(BackColor.rgb, 0., 1.);\n");
            ShadAppend(buff);
        }
        if (state->light_separate) {
            ShadAppend("SecColor.rgb = clamp(SecColor.rgb, 0., 1.);\n");
            if (twosided) {
                ShadAppend("SecBackColor.rgb = clamp(SecBackColor.rgb, 0., 1.);\n");
            }
        }
        if (is_default && need) {
            if (need->need_color > 0) ShadAppend("gl_FrontColor = Color;\n");
            if (need->need_color > 1) ShadAppend("gl_BackColor = BackColor;\n");
            if (need->need_secondary > 0) ShadAppend("gl_FrontSecondaryColor = SecColor;\n");
            if (need->need_secondary > 1) ShadAppend("gl_BackSecondaryColor = SecBackColor;\n");
        }
    }
    // calculate texture coordinates
    if (comments) ShadAppend("// texturing\n");
    if (texgens) ShadAppend("vec4 tmp_tcoor;\n");
    int spheremap = 0;
    int reflectmap = 0;
    if (texmats) ShadAppend("vec4 tmp_tex;\n");
    for (int i = 0; i < hardext.maxtex; i++) {
        int t = state->texture[i].textype;
        if (need && (need->need_texs & (1 << i)) && t == 0) t = 1;
        int mat = state->texture[i].texmat;
        int adjust = state->texture[i].texadjust;
        int tg[4];
        tg[0] = state->texgen[i].texgen_s;
        tg[1] = state->texgen[i].texgen_t;
        tg[2] = state->texgen[i].texgen_r;
        tg[3] = state->texgen[i].texgen_q;
        int ntc = texnsize[t - 1];
        if (t) {
            if (comments) {
                sprintf(buff, "// texture %d active: %X %s %s\n", i, t, mat ? "with matrix" : "",
                        adjust ? "npot adjusted" : "");
                ShadAppend(buff);
            }
            char texcoord[50];
            if (tg[0] || tg[1] || tg[2] || tg[3]) {
                // One or more texgen is active
                if (tg[0])
                    tg[0] = state->texgen[i].texgen_s_mode;
                else
                    tg[0] = FPE_TG_NONE;
                if (tg[1])
                    tg[1] = state->texgen[i].texgen_t_mode;
                else
                    tg[1] = FPE_TG_NONE;
                if (tg[2])
                    tg[2] = state->texgen[i].texgen_r_mode;
                else
                    tg[2] = FPE_TG_NONE;
                if (tg[3])
                    tg[3] = state->texgen[i].texgen_q_mode;
                else
                    tg[3] = FPE_TG_NONE;
                if (comments) {
                    sprintf(buff, "//  texgen %d / %d / %d / %d\n", tg[0], tg[1], tg[2], tg[3]);
                    ShadAppend(buff);
                }
                sprintf(texcoord, "tmp_tcoor");
                ShadAppend("tmp_tcoor=vec4(0., 0., 0., 1.);\n");
                if (mat) {
                    ntc = 4;
                }
                for (int j = 0; j < ntc; j++) {
                    if (tg[j] == FPE_TG_NORMALMAP) {
                        need_normal = 1;
                        if (j == 0 && tg[j + 1] == FPE_TG_NORMALMAP) {
                            sprintf(buff, "tmp_tcoor.%c%c=normal.%c%c;\n", texcoordxy[j], texcoordxy[j + 1],
                                    texcoordxy[j], texcoordxy[j + 1]);
                            ++j;
                        } else
                            sprintf(buff, "tmp_tcoor.%c=normal.%c;\n", texcoordxy[j], texcoordxy[j]);
                    } else if (tg[j] == FPE_TG_SPHEREMAP) {
                        if (!spheremap) {
                            spheremap = 1;
                            if (!need_vertex) need_vertex = 1;
                            need_normal = 1;
                            ShadAppend("vec3 tmpsphere = reflect(normalize(vertex.xyz), normal);\n");
                            ShadAppend("tmpsphere.z+=1.0;\n");
                            if (j == 0 && tg[j + 1] == FPE_TG_SPHEREMAP)
                                sprintf(buff, "tmp_tcoor.xy = tmpsphere.xy*(0.5*inversesqrt(dot(tmpsphere, "
                                              "tmpsphere))) + vec2(0.5);");
                            else
                                ShadAppend("tmpsphere.xy = tmpsphere.xy*(0.5*inversesqrt(dot(tmpsphere, tmpsphere))) + "
                                           "vec2(0.5);");
                        }
                        if (j == 0 && tg[j + 1] == FPE_TG_SPHEREMAP)
                            ++j;
                        else
                            sprintf(buff, "tmp_tcoor.%c=tmpsphere.%c;\n", texcoordxy[j], texcoordxy[j]);
                    } else if (tg[j] == FPE_TG_OBJLINEAR) {
                        sprintf(buff, "tmp_tcoor.%c=dot(gl_Vertex, _gl4es_ObjectPlane%c_%d);\n", texcoordxy[j],
                                texcoordNAME[j], i);
                        need_objplane[i][j] = 1;
                    } else if (tg[j] == FPE_TG_EYELINEAR) {
                        sprintf(buff, "tmp_tcoor.%c=dot(vertex, _gl4es_EyePlane%c_%d);\n", texcoordxy[j],
                                texcoordNAME[j], i);
                        need_eyeplane[i][j] = 1;
                        if (!need_vertex) need_vertex = 1;
                    } else if (tg[j] == FPE_TG_REFLECMAP) {
                        if (!reflectmap) {
                            reflectmap = 1;
                            if (!need_vertex) need_vertex = 1;
                            need_normal = 1;
                            if (j == 0 && tg[j + 1] == FPE_TG_REFLECMAP && tg[j + 2] == FPE_TG_REFLECMAP)
                                sprintf(buff, "tmp_tcoor.xyz = reflect(normalize(vertex.xyz), normal);\n");
                            else
                                ShadAppend("vec3 tmpreflect = reflect(normalize(vertex.xyz), normal);\n");
                        }
                        if (j == 0 && tg[j + 1] == FPE_TG_REFLECMAP && tg[j + 2] == FPE_TG_REFLECMAP)
                            j += 2;
                        else
                            sprintf(buff, "tmp_tcoor.%c=tmpreflect.%c;\n", texcoordxy[j], texcoordxy[j]);
                    } else if (tg[j] == FPE_TG_NONE) {
                        sprintf(buff, "tmp_tcoor.%c=gl_MultiTexCoord%d.%c;\n", texcoordxy[j], i, texcoordxy[j]);
                    }
                    ShadAppend(buff);
                }
            } else {
                sprintf(texcoord, "gl_MultiTexCoord%d", i);
            }
            if (mat) {
                // it would be better to use texture2Dproj in fragment shader, but that will complicate the varying
                // definition...
                sprintf(buff, "tmp_tex = (_gl4es_TextureMatrix_%d * %s);\n", i, texcoord);
                ShadAppend(buff);

                sprintf(buff, "_gl4es_TexCoord_%d = tmp_tex.%s / tmp_tex.q;\n", i, texxyzsize[t - 1]);
                // sprintf(buff, "_gl4es_TexCoord_%d = (_gl4es_TextureMatrix_%d * %s).%s;\n", i, i, texcoord,
                // texxyzsize[t-1]);
            } else
                sprintf(buff, "_gl4es_TexCoord_%d = %s.%s / %s.q;\n", i, texcoord, texxyzsize[t - 1], texcoord);

            ShadAppend(buff);
            if (adjust) {
                need_adjust[i] = 1;
                sprintf(buff, "_gl4es_TexCoord_%d.xy *= _gl4es_TexAdjust_%d;\n", i,
                        i); // to avoid error on Cube map... but will that work anyway?
                ShadAppend(buff);
            }
        }
    }
    // point sprite special case
    if (point) {
        if (!need_vertex) need_vertex = 1;
        ShadAppend("float ps_d = length(vertex);\n");
        sprintf(buff, "gl_PointSize = clamp(gl_Point.size*inversesqrt(gl_Point.distanceConstantAttenuation + "
                      "ps_d*(gl_Point.distanceLinearAttenuation + ps_d*gl_Point.distanceQuadraticAttenuation)), "
                      "gl_Point.sizeMin, gl_Point.sizeMax);\n");
        ShadAppend(buff);
    }
    // insert normal, vertex and eye/obj planes if needed
    if (need_vertex) {
        buff[0] = '\0';
        if (need_vertex == 1) strcat(buff, "vec4 ");
        strcat(buff, "vertex = gl_ModelViewMatrix * gl_Vertex;\n");
        shad = InplaceInsert(GetLine(shad, normal_line + headers), buff, shad, &shad_cap);
        normal_line += CountLine(buff);
    }
    if (need_normal) {
#if 0
        if(state->rescaling)
            strcpy(buff, "vec3 normal = gl_NormalScale*(gl_NormalMatrix * gl_Normal);\n");
        else
            strcpy(buff, "vec3 normal = gl_NormalMatrix * gl_Normal;\n");
        if(state->normalize)
            strcat(buff, "normal = normalize(normal);\n");
#else
        // Implementions may choose to normalize for rescale...
        // TODO deal better with rescale ?
        // if(state->rescaling || state->normalize)
        strcpy(buff, "vec3 normal = normalize(gl_NormalMatrix * gl_Normal);\n");
        // else
        // strcpy(buff, "vec3 normal = gl_NormalMatrix * gl_Normal;\n");
#endif
        shad = InplaceInsert(GetLine(shad, normal_line + headers), buff, shad, &shad_cap);
    }
    buff[0] = '\0';
    for (int i = 0; i < MAX_TEX; i++) {
        char tmp[100];
        for (int j = 0; j < 4; j++) {
            if (need_objplane[i][j]) {
                sprintf(tmp, "uniform vec4 _gl4es_ObjectPlane%c_%d;\n", texcoordNAME[j], i);
                strcat(buff, tmp);
            }
            if (need_eyeplane[i][j]) {
                sprintf(tmp, "uniform vec4 _gl4es_EyePlane%c_%d;\n", texcoordNAME[j], i);
                strcat(buff, tmp);
            }
        }
        if (need_adjust[i]) {
            sprintf(tmp, "uniform vec2 _gl4es_TexAdjust_%d;\n", i);
            strcat(buff, tmp);
        }
    }
    if (buff[0] != '\0') {
        shad = InplaceInsert(GetLine(shad, headers), buff, shad, &shad_cap);
        headers += CountLine(buff);
    }
    if (fog) {
        if (comments) {
            sprintf(buff, "// Fog On: mode=%X, source=%X distance=%X\n", fogmode, fogsource, fogdist);
            ShadAppend(buff);
        }
#if 0 // vertex fog
        char fogsrc[50];
        if(fogsource==FPE_FOG_SRC_COORD)
            strcpy(fogsrc, "FogCoord");
        else switch(fogdist) {
            case FPE_FOG_DIST_RADIAL: strcpy(fogsrc, "length(vertex.xyz)"); break;
            case FPE_FOG_DIST_PLANE: strcpy(fogsrc, "vertex.z"); break;
            default: strcpy(fogsrc, "abs(vertex.z)");
        }
        sprintf(buff, "float fog_c = %s;\n", fogsrc);
        ShadAppend(buff);
        switch(fogmode) {
            case FPE_FOG_EXP:
                ShadAppend("FogF = clamp(exp(-gl_Fog.density * fog_c), 0., 1.);\n");
                break;
            case FPE_FOG_EXP2:
                ShadAppend("FogF = clamp(exp(-(gl_Fog.density * fog_c)*(gl_Fog.density * fog_c)), 0., 1.);\n");
                break;
            case FPE_FOG_LINEAR:
                ShadAppend("FogF = clamp((gl_Fog.end - fog_c) * gl_Fog.scale, 0., 1.);\n");
                break;
        }
#else // pixel fog
        if (fogsource == FPE_FOG_SRC_COORD)
            sprintf(buff, "FogSrc = FogCoord;\n");
        else
            switch (fogdist) {
            case FPE_FOG_DIST_RADIAL:
                sprintf(buff, "FogSrc = vertex.xyz;\n");
                break;
            case FPE_FOG_DIST_PLANE:
            default:
                sprintf(buff, "FogSrc = vertex.z;\n");
            }
        ShadAppend(buff);
#endif
    }

    ShadAppend("}\n");

    DBG(SHUT_LOGD("FPE Shader: \n%s\n", shad);)

    return (const char* const*)&shad;
}

const char* const* fpe_FragmentShader(shaderconv_need_t* need, fpe_state_t* state) {
    // state can be NULL, so provide a 0 default
    if (!shad_cap) shad_cap = 1024;
    if (!shad) shad = (char*)malloc(shad_cap);
    fpe_state_t default_state = {0};
    int is_default = !need;
    if (!state) state = &default_state;
    int headers = 0;
    int lighting = state->lighting;
    int twosided = state->twosided && lighting;
    int light_separate = state->light_separate && lighting;
    int secondary = (state->colorsum && !(lighting && light_separate)) || fpe_texenvSecondary(state);
    int alpha_test = state->alphatest;
    int alpha_func = state->alphafunc;
    int fog = state->fog || (need && need->need_fogcoord);
    int fogsource = state->fogsource;
    int fogmode = state->fogmode;
    int fogdist = state->fogdist;
    int planes = state->plane;
    int point = state->point;
    int pointsprite = state->pointsprite;
    int pointsprite_coord = state->pointsprite_coord;
    int pointsprite_upper = state->pointsprite_upper;
    int texenv_combine = 0;
    int texturing = 0;
    char buff[1024];
    const char* fogp = hardext.highp ? "highp" : "mediump";

    strcpy(shad, fpeshader_signature);

    // check texture streaming and texturing
    {
        int need_stream = 0;
        for (int i = 0; i < hardext.maxtex; i++) {
            const int t = state->texture[i].textype;
            if (t == FPE_TEX_STRM) need_stream = 1;
            if (t) ++texturing;
        }
        if (need_stream) ShadAppend("#extension GL_IMG_texture_stream2 : enable\n");
    }

    if (comments) {
        sprintf(buff,
                "// ** Fragment Shader **\n// lighting=%d, alpha=%d, secondary=%d, planes=%s, texturing=%d point=%d\n",
                lighting, alpha_test, secondary, fpe_binary(planes, 6), texturing, point);
        ShadAppend(buff);
        headers += CountLine(buff);
    }
    ShadAppend("varying vec4 Color;\n");
    headers++;
    if (twosided) {
        ShadAppend("varying vec4 BackColor;\n");
        headers++;
    }
    if (light_separate || secondary) {
        ShadAppend("varying vec4 SecColor;\n");
        headers++;
        if (twosided) {
            ShadAppend("varying vec4 SecBackColor;\n");
            headers++;
        }
    }
    if (fog) {
#if 0 // vertex fog
        ShadAppend("varying mediump float FogF;\n");
        headers++;
#else // pixel fog
        if (fogsource == FPE_FOG_SRC_COORD) {
            sprintf(buff, "varying %s float FogSrc;\n", fogp);
        } else
            switch (fogdist) {
            case FPE_FOG_DIST_RADIAL:
                sprintf(buff, "varying %s vec3 FogSrc;\n", fogp);
                break;
            case FPE_FOG_DIST_PLANE:
                sprintf(buff, "varying %s float FogSrc;\n", fogp);
                break;
            default:
                sprintf(buff, "varying %s float FogSrc;\n", fogp);
            }
        ShadAppend(buff);
#endif
    }
    if (planes) {
        // ShadAppend("varying vec4 clipvertex;\n");
        for (int i = 0; i < hardext.maxplanes; i++) {
            if ((planes >> i) & 1) {
                sprintf(buff, "varying mediump float clippedvertex_%d;\n", i);
                ShadAppend(buff);
                headers++;
            }
        }
    }
    /*if(fog && fogsource==FPE_FOG_SRC_COORD) {
        ShadAppend("varying float FogCoord;\n");
        headers++;
    }*/
    // textures coordinates
    for (int i = 0; i < hardext.maxtex; i++) {
        int t = state->texture[i].textype;
        if (point && !pointsprite) t = 0;
        if (t) {
            sprintf(buff, "varying %s _gl4es_TexCoord_%d;\n", texvecsize[t - 1], i);
            ShadAppend(buff);
            sprintf(buff, "uniform %s _gl4es_TexSampler_%d;\n", texsampler[t - 1], i);
            ShadAppend(buff);
            headers++;

            int texenv = state->texenv[i].texenv;
            if (texenv >= FPE_COMBINE) {
                int n = 1 + texenv - FPE_COMBINE;
                if (n > texenv_combine) texenv_combine = n;
                if (state->texenv[i].texrgbscale) {
                    sprintf(buff, "uniform float _gl4es_TexEnvRGBScale_%d;\n", i);
                    ShadAppend(buff);
                    headers++;
                }
                if (state->texenv[i].texalphascale) {
                    sprintf(buff, "uniform float _gl4es_TexEnvAlphaScale_%d;\n", i);
                    ShadAppend(buff);
                    headers++;
                }
            }
        }
    }
    if (alpha_test && alpha_func > FPE_NEVER) {
        ShadAppend(gl4es_alphaRefSource);
        headers++;
    }

    ShadAppend("void main() {\n");

    //*** Clip Planes (it's probably not the best idea to do that here...)
    if (planes) {
        ShadAppend("if((");
        int k = 0;
        for (int i = 0; i < hardext.maxplanes; i++) {
            if ((planes >> i) & 1) {
                // sprintf(buff, "%smin(0., dot(clipvertex, gl_ClipPlane[%d]))", k?"+":"",  i);
                sprintf(buff, "%smin(0., clippedvertex_%d)", k ? "+" : "", i);
                ShadAppend(buff);
                k = 1;
            }
        }
        ShadAppend(")<0.) discard;\n");
    }

    //*** initial color
    sprintf(buff, "vec4 fColor = %s;\n", twosided ? "(gl_FrontFacing)?Color:BackColor" : "Color");
    ShadAppend(buff);

    //*** apply textures
    if (texturing && (!point || pointsprite)) {
        // fetch textures first
        for (int i = 0; i < hardext.maxtex; i++) {
            int t = state->texture[i].textype;
            if (t) {
                if (point && pointsprite && pointsprite_coord) {
                    if (pointsprite_upper)
                        sprintf(
                            buff,
                            "vec4 texColor%d = %s(_gl4es_TexSampler_%d, vec2(gl_PointCoord.x, 1.-gl_PointCoord.y));\n",
                            i, texnoproj[t - 1], i);
                    else
                        sprintf(buff, "vec4 texColor%d = %s(_gl4es_TexSampler_%d, gl_PointCoord);\n", i,
                                texnoproj[t - 1], i);
                } else
                    sprintf(buff, "vec4 texColor%d = %s(_gl4es_TexSampler_%d, _gl4es_TexCoord_%d);\n", i,
                            texname[t - 1], i, i);
                ShadAppend(buff);
            }
        }

        // TexEnv stuff
        if (texenv_combine > 0) {
            if (texenv_combine == 2)
                ShadAppend("vec4 Arg0, Arg1, Arg2, Arg3;\n");
            else
                ShadAppend("vec4 Arg0, Arg1, Arg2;\n");
        }
        // fetch textures first
        for (int i = 0; i < hardext.maxtex; i++) {
            int t = state->texture[i].textype;

            if (t) {
                int texenv = state->texenv[i].texenv;
                int texformat = state->texture[i].texformat;
                if (comments) {
                    sprintf(buff, "// Texture %d active: %X, texenv=%X, format=%X\n", i, t, texenv, texformat);
                    ShadAppend(buff);
                }
                int needclamp = 1;
                switch (texenv) {
                case FPE_MODULATE:
                    if (texformat == FPE_TEX_RGB || texformat == FPE_TEX_LUM) {
                        sprintf(buff, "fColor.rgb *= texColor%d.rgb;\n", i);
                        ShadAppend(buff);
                    } else if (texformat == FPE_TEX_ALPHA) {
                        sprintf(buff, "fColor.a *= texColor%d.a;\n", i);
                        ShadAppend(buff);
                    } else {
                        sprintf(buff, "fColor *= texColor%d;\n", i);
                        ShadAppend(buff);
                    }
                    needclamp = 0;
                    break;
                case FPE_ADD:
                    if (texformat != FPE_TEX_ALPHA) {
                        sprintf(buff, "fColor.rgb += texColor%d.rgb;\n", i);
                        ShadAppend(buff);
                    }
                    if (texformat == FPE_TEX_INTENSITY || texformat == FPE_TEX_DEPTH)
                        sprintf(buff, "fColor.a += texColor%d.a;\n", i);
                    else
                        sprintf(buff, "fColor.a *= texColor%d.a;\n", i);
                    ShadAppend(buff);
                    break;
                case FPE_DECAL:
                    sprintf(buff, "fColor.rgb = mix(fColor.rgb, texColor%d.rgb, texColor%d.a);\n", i, i);
                    ShadAppend(buff);
                    needclamp = 0;
                    break;
                case FPE_BLEND:
                    // create the Uniform for TexEnv Constant color
                    sprintf(buff, "uniform lowp vec4 _gl4es_TextureEnvColor_%d;\n", i);
                    shad = InplaceInsert(GetLine(shad, headers), buff, shad, &shad_cap);
                    headers += CountLine(buff);
                    needclamp = 0;
                    if (texformat != FPE_TEX_ALPHA) {
                        sprintf(buff, "fColor.rgb = mix(fColor.rgb, _gl4es_TextureEnvColor_%d.rgb, texColor%d.rgb);\n",
                                i, i);
                        ShadAppend(buff);
                    }
                    switch (texformat) {
                    case FPE_TEX_LUM:
                    case FPE_TEX_RGB:
                        // no change in alpha channel
                        break;
                    case FPE_TEX_INTENSITY:
                    case FPE_TEX_DEPTH:
                        sprintf(buff, "fColor.a = mix(fColor.a, _gl4es_TextureEnvColor_%d.a, texColor%d.a);\n", i, i);
                        ShadAppend(buff);
                        break;
                    default:
                        sprintf(buff, "fColor.a *= texColor%d.a;\n", i);
                        ShadAppend(buff);
                    }
                    ShadAppend(buff);
                    break;
                case FPE_REPLACE:
                    if (texformat == FPE_TEX_RGB || texformat == FPE_TEX_LUM) {
                        sprintf(buff, "fColor.rgb = texColor%d.rgb;\n", i);
                        ShadAppend(buff);
                    } else if (texformat == FPE_TEX_ALPHA) {
                        sprintf(buff, "fColor.a = texColor%d.a;\n", i);
                        ShadAppend(buff);
                    } else {
                        sprintf(buff, "fColor = texColor%d;\n", i);
                        ShadAppend(buff);
                    }
                    needclamp = 0;
                    break;
                case FPE_COMBINE:
                case FPE_COMBINE4: {
                    int constant = 0;
                    // parse the combine state
                    int combine_rgb = state->texcombine[i] & 0xf;
                    int combine_alpha = (state->texcombine[i] >> 4) & 0xf;
                    int src_r[4], op_r[4];
                    int src_a[4], op_a[4];
                    src_a[0] = state->texenv[i].texsrcalpha0;
                    op_a[0] = state->texenv[i].texopalpha0;
                    src_r[0] = state->texenv[i].texsrcrgb0;
                    op_r[0] = state->texenv[i].texoprgb0;
                    src_a[1] = state->texenv[i].texsrcalpha1;
                    op_a[1] = state->texenv[i].texopalpha1;
                    src_r[1] = state->texenv[i].texsrcrgb1;
                    op_r[1] = state->texenv[i].texoprgb1;
                    src_a[2] = state->texenv[i].texsrcalpha2;
                    op_a[2] = state->texenv[i].texopalpha2;
                    src_r[2] = state->texenv[i].texsrcrgb2;
                    op_r[2] = state->texenv[i].texoprgb2;
                    src_a[3] = state->texenv[i].texsrcalpha3;
                    op_a[3] = state->texenv[i].texopalpha3;
                    src_r[3] = state->texenv[i].texsrcrgb3;
                    op_r[3] = state->texenv[i].texoprgb3;
                    if (combine_rgb == FPE_CR_DOT3_RGBA) {
                        src_a[0] = src_a[1] = src_a[2] = -1;
                        op_a[0] = op_a[1] = op_a[2] = -1;
                        src_r[2] = op_r[2] = -1;
                        src_r[3] = op_r[3] = -1;
                        src_a[3] = op_a[3] = -1;
                    } else {
                        if (combine_alpha == FPE_CR_REPLACE) {
                            src_a[1] = src_a[2] = src_a[3] = -1;
                            op_a[1] = op_a[2] = op_a[3] = -1;
                        } else if (combine_alpha >= FPE_CR_MOD_ADD || combine_alpha == FPE_CR_INTERPOLATE) {
                            // need all 3
                            src_a[3] = -1;
                            op_a[3] = -1;
                        } else {
                            if (texenv == FPE_COMBINE4 &&
                                (combine_alpha == FPE_CR_ADD || combine_alpha == FPE_CR_ADD_SIGNED)) {
                                // need all 4
                            } else {
                                src_a[2] = src_a[3] = -1;
                                op_a[2] = op_a[3] = -1;
                            }
                        }
                        if (combine_rgb == FPE_CR_REPLACE) {
                            src_r[1] = src_r[2] = src_r[3] = -1;
                            op_r[1] = op_r[2] = op_r[3] = -1;
                        } else if (combine_rgb >= FPE_CR_MOD_ADD || combine_rgb == FPE_CR_INTERPOLATE) {
                            // need all 3
                            src_r[3] = -1;
                            op_r[3] = -1;
                        } else {
                            if (texenv == FPE_COMBINE4 &&
                                (combine_rgb == FPE_CR_ADD || combine_rgb == FPE_CR_ADD_SIGNED)) {
                                // need all 4
                            } else {
                                src_r[2] = src_r[3] = -1;
                                op_r[2] = op_r[3] = -1;
                            }
                        }
                    }
                    // is texture constants needed ?
                    for (int j = 0; j < 4; j++) {
                        if (src_a[j] == FPE_SRC_CONSTANT || src_r[j] == FPE_SRC_CONSTANT) constant = 1;
                    }
                    if (comments) {
                        sprintf(buff, " //  Combine RGB: fct=%d, Src/Op: 0=%d/%d 1=%d/%d 2=%d/%d 3=%d/%d\n",
                                combine_rgb, src_r[0], op_r[0], src_r[1], op_r[1], src_r[2], op_r[2], src_r[3],
                                op_r[3]);
                        ShadAppend(buff);
                        sprintf(buff, " //  Combine Alpha: fct=%d, Src/Op: 0=%d/%d 1=%d/%d 2=%d/%d 3=%d/%d\n",
                                combine_alpha, src_a[0], op_a[0], src_a[1], op_a[1], src_a[2], op_a[2], src_a[3],
                                op_a[3]);
                        ShadAppend(buff);
                    }
                    if (constant) {
                        // yep, create the Uniform
                        sprintf(buff, "uniform lowp vec4 _gl4es_TextureEnvColor_%d;\n", i);
                        shad = InplaceInsert(GetLine(shad, headers), buff, shad, &shad_cap);
                        headers += CountLine(buff);
                    }
                    for (int j = 0; j < 4; j++) {
                        if (src_r[j] == src_a[j] && op_r[j] == FPE_OP_SRCCOLOR && op_a[j] == FPE_OP_ALPHA) {
                            sprintf(buff, "Arg%d = %s;\n", j, fpe_texenvSrc(src_r[j], i, twosided));
                            ShadAppend(buff);
                        } else if (src_r[j] == src_a[j] && op_r[j] == FPE_OP_MINUSCOLOR &&
                                   op_a[j] == FPE_OP_MINUSALPHA) {
                            sprintf(buff, "Arg%d = vec4(1.) - %s;\n", j, fpe_texenvSrc(src_r[j], i, twosided));
                            ShadAppend(buff);
                        } else {
                            if (op_r[j] != -1) switch (op_r[j]) {
                                case FPE_OP_SRCCOLOR:
                                    sprintf(buff, "Arg%d.rgb = %s.rgb;\n", j, fpe_texenvSrc(src_r[j], i, twosided));
                                    ShadAppend(buff);
                                    break;
                                case FPE_OP_MINUSCOLOR:
                                    sprintf(buff, "Arg%d.rgb = vec3(1.) - %s.rgb;\n", j,
                                            fpe_texenvSrc(src_r[j], i, twosided));
                                    ShadAppend(buff);
                                    break;
                                case FPE_OP_ALPHA:
                                    sprintf(buff, "Arg%d.rgb = vec3(%s.a);\n", j, fpe_texenvSrc(src_r[j], i, twosided));
                                    ShadAppend(buff);
                                    break;
                                case FPE_OP_MINUSALPHA:
                                    sprintf(buff, "Arg%d.rgb = vec3(1. - %s.a);\n", j,
                                            fpe_texenvSrc(src_r[j], i, twosided));
                                    ShadAppend(buff);
                                    break;
                                }
                            if (op_a[j] != -1) switch (op_a[j]) {
                                case FPE_OP_ALPHA:
                                    sprintf(buff, "Arg%d.a = %s.a;\n", j, fpe_texenvSrc(src_a[j], i, twosided));
                                    ShadAppend(buff);
                                    break;
                                case FPE_OP_MINUSALPHA:
                                    sprintf(buff, "Arg%d.a = 1. - %s.a;\n", j, fpe_texenvSrc(src_a[j], i, twosided));
                                    ShadAppend(buff);
                                    break;
                                }
                        }
                    }
                    if (combine_rgb != FPE_CR_DOT3_RGBA && combine_rgb != FPE_CR_DOT3_RGB &&
                        combine_rgb == combine_alpha) {
                        switch (combine_alpha) {
                        case FPE_CR_REPLACE:
                            ShadAppend("fColor = Arg0;\n");
                            break;
                        case FPE_CR_MODULATE:
                            ShadAppend("fColor = Arg0 * Arg1;\n");
                            break;
                        case FPE_CR_ADD:
                            if (texenv == FPE_COMBINE4)
                                ShadAppend("fColor = Arg0*Arg1 + Arg2*Arg3;\n");
                            else
                                ShadAppend("fColor = Arg0 + Arg1;\n");
                            break;
                        case FPE_CR_ADD_SIGNED:
                            if (texenv == FPE_COMBINE4)
                                ShadAppend("fColor = Arg0*Arg1 + Arg2*Arg3 - vec4(0.5);\n");
                            else
                                ShadAppend("fColor = Arg0 + Arg1 - vec4(0.5);\n");
                            break;
                        case FPE_CR_INTERPOLATE:
                            ShadAppend("fColor = Arg0*Arg2 + Arg1*(vec4(1.)-Arg2);\n");
                            break;
                        case FPE_CR_SUBTRACT:
                            ShadAppend("fColor = Arg0 - Arg1;\n");
                            break;
                        case FPE_CR_MOD_ADD:
                            ShadAppend("fColor = Arg0*Arg2 + Arg1;\n");
                            break;
                        case FPE_CR_MOD_ADD_SIGNED:
                            ShadAppend("fColor = Arg0*Arg2 + Arg1 - vec4(0.5);\n");
                            break;
                        case FPE_CR_MOD_SUB:
                            ShadAppend("fColor = Arg0*Arg2 - Arg1;\n");
                            break;
                        }
                    } else {
                        switch (combine_rgb) {
                        case FPE_CR_REPLACE:
                            ShadAppend("fColor.rgb = Arg0.rgb;\n");
                            break;
                        case FPE_CR_MODULATE:
                            ShadAppend("fColor.rgb = Arg0.rgb * Arg1.rgb;\n");
                            break;
                        case FPE_CR_ADD:
                            if (texenv == FPE_COMBINE4)
                                ShadAppend("fColor.rgb = Arg0.rgb*Arg1.rgb + Arg2.rgb*Arg3.rgb;\n");
                            else
                                ShadAppend("fColor.rgb = Arg0.rgb + Arg1.rgb;\n");
                            break;
                        case FPE_CR_ADD_SIGNED:
                            if (texenv == FPE_COMBINE4)
                                ShadAppend("fColor.rgb = Arg0.rgb*Arg1.rgb + Arg2.rgb*Arg3.rgb - vec3(0.5);\n");
                            else
                                ShadAppend("fColor.rgb = Arg0.rgb + Arg1.rgb - vec3(0.5);\n");
                            break;
                        case FPE_CR_INTERPOLATE:
                            ShadAppend("fColor.rgb = Arg0.rgb*Arg2.rgb + Arg1.rgb*(vec3(1.)-Arg2.rgb);\n");
                            break;
                        case FPE_CR_SUBTRACT:
                            ShadAppend("fColor.rgb = Arg0.rgb - Arg1.rgb;\n");
                            break;
                        case FPE_CR_DOT3_RGB:
                            ShadAppend("fColor.rgb = "
                                       "vec3(4.*((Arg0.r-0.5)*(Arg1.r-0.5)+(Arg0.g-0.5)*(Arg1.g-0.5)+(Arg0.b-0.5)*("
                                       "Arg1.b-0.5)));\n");
                            break;
                        case FPE_CR_DOT3_RGBA:
                            ShadAppend("fColor = "
                                       "vec4(4.*((Arg0.r-0.5)*(Arg1.r-0.5)+(Arg0.g-0.5)*(Arg1.g-0.5)+(Arg0.b-0.5)*("
                                       "Arg1.b-0.5)));\n");
                            break;
                        case FPE_CR_MOD_ADD:
                            ShadAppend("fColor.rgb = Arg0.rgb*Arg2.rgb + Arg1.rgb;\n");
                            break;
                        case FPE_CR_MOD_ADD_SIGNED:
                            ShadAppend("fColor.rgb = Arg0.rgb*Arg2.rgb + Arg1.rgb - vec3(0.5);\n");
                            break;
                        case FPE_CR_MOD_SUB:
                            ShadAppend("fColor.rgb = Arg0.rgb*Arg2.rgb - Arg1.rgb;\n");
                            break;
                        }
                        if (combine_rgb != FPE_CR_DOT3_RGBA) switch (combine_alpha) {
                            case FPE_CR_REPLACE:
                                ShadAppend("fColor.a = Arg0.a;\n");
                                break;
                            case FPE_CR_MODULATE:
                                ShadAppend("fColor.a = Arg0.a * Arg1.a;\n");
                                break;
                            case FPE_CR_ADD:
                                if (texenv == FPE_COMBINE4)
                                    ShadAppend("fColor.a = Arg0.a*Arg1.a + Arg2.a*Arg3.a;\n");
                                else
                                    ShadAppend("fColor.a = Arg0.a + Arg1.a;\n");
                                break;
                            case FPE_CR_ADD_SIGNED:
                                if (texenv == FPE_COMBINE4)
                                    ShadAppend("fColor.a = Arg0.a*Arg1.a + Arg2.a*Arg3.a - 0.5;\n");
                                else
                                    ShadAppend("fColor.a = Arg0.a + Arg1.a - 0.5;\n");
                                break;
                            case FPE_CR_INTERPOLATE:
                                ShadAppend("fColor.a = Arg0.a*Arg2.a + Arg1.a*(1.-Arg2.a);\n");
                                break;
                            case FPE_CR_SUBTRACT:
                                ShadAppend("fColor.a = Arg0.a - Arg1.a;\n");
                                break;
                            case FPE_CR_MOD_ADD:
                                ShadAppend("fColor.a = Arg0.a*Arg2.a + Arg1.a;\n");
                                break;
                            case FPE_CR_MOD_ADD_SIGNED:
                                ShadAppend("fColor.a = Arg0.a*Arg2.a + Arg1.a - 0.5;\n");
                                break;
                            case FPE_CR_MOD_SUB:
                                ShadAppend("fColor.a = Arg0.a*Arg2.a - Arg1.a;\n");
                                break;
                            }
                    }
                    if ((state->texenv[i].texrgbscale) && (state->texenv[i].texalphascale)) {
                        sprintf(buff, "fColor *= _gl4es_TexEnvRGBScale_%d;\n", i);
                        ShadAppend(buff);
                    } else {
                        if (state->texenv[i].texrgbscale) {
                            sprintf(buff, "fColor.rgb *= _gl4es_TexEnvRGBScale_%d;\n", i);
                            ShadAppend(buff);
                        }
                        if (state->texenv[i].texalphascale) {
                            sprintf(buff, "fColor.a *= _gl4es_TexEnvAlphaScale_%d;\n", i);
                            ShadAppend(buff);
                        }
                    }
                } break;
                }
                if (needclamp) ShadAppend("fColor = clamp(fColor, 0., 1.);\n");
            }
        }
    }
    //*** Alpha Test
    if (alpha_test) {
        if (comments) {
            sprintf(buff, "// Alpha Test, fct=%X\n", alpha_func);
            ShadAppend(buff);
        }
        if (alpha_func == FPE_ALWAYS) {
            // nothing here...
        } else if (alpha_func == FPE_NEVER) {
            ShadAppend("discard;\n"); // Never pass...
        } else {
            // FPE_LESS FPE_EQUAL FPE_LEQUAL FPE_GREATER FPE_NOTEQUAL FPE_GEQUAL
            // but need to negate the operator
            const char* alpha_test_op[] = {">=", "!=", ">", "<=", "==", "<"};
            sprintf(buff, "if (floor(fColor.a*255.) %s _gl4es_AlphaRef) discard;\n",
                    alpha_test_op[alpha_func - FPE_LESS]);
            ShadAppend(buff);
        }
    }

    //*** Add secondary color
    if (light_separate || secondary) {
        if (comments) {
            sprintf(buff, "// Add Secondary color (%s %s)\n", light_separate ? "light" : "",
                    secondary ? "secondary" : "");
            ShadAppend(buff);
        }
        sprintf(buff, "fColor.rgb += (%s).rgb;\n", twosided ? "(gl_FrontFacing)?SecColor:SecBackColor" : "SecColor");
        ShadAppend(buff);
        ShadAppend("fColor.rgb = clamp(fColor.rgb, 0., 1.);\n");
    }

    //*** Fog
    if (fog) {
        if (comments) {
            sprintf(buff, "// Fog On: mode=%X, source=%X\n", fogmode, fogsource);
            ShadAppend(buff);
        }
#if 0 // vertex fog
        ShadAppend("fColor.rgb = mix(gl_Fog.color.rgb, fColor.rgb, FogF);\n");
#else // pixel fog
        char fogsrc[50];
        if (fogsource == FPE_FOG_SRC_COORD)
            strcpy(fogsrc, "FogSrc");
        else
            switch (fogdist) {
            case FPE_FOG_DIST_RADIAL:
                strcpy(fogsrc, "length(FogSrc)");
                break;
            case FPE_FOG_DIST_PLANE:
                strcpy(fogsrc, "FogSrc");
                break;
            default:
                strcpy(fogsrc, "abs(FogSrc)");
            }
        sprintf(buff, "%s float fog_c = %s;\n", fogp, fogsrc);
        ShadAppend(buff);
        switch (fogmode) {
        case FPE_FOG_EXP:
            sprintf(buff, "%s float FogF = clamp(exp(-gl_Fog.density * fog_c), 0., 1.);\n", fogp);
            break;
        case FPE_FOG_EXP2:
            sprintf(buff, "%s float FogF = clamp(exp(-(gl_Fog.density * fog_c)*(gl_Fog.density * fog_c)), 0., 1.);\n",
                    fogp);
            break;
        case FPE_FOG_LINEAR:
            sprintf(buff, "%s float FogF = clamp((gl_Fog.end - fog_c) %s, 0., 1.);\n", fogp,
                    hardext.highp ? "* gl_Fog.scale" : "/ (gl_Fog.end - gl_Fog.start)");
            break;
        }
        ShadAppend(buff);
        ShadAppend("fColor.rgb = mix(gl_Fog.color.rgb, fColor.rgb, FogF);\n");
#endif
    }

    // done
    ShadAppend("gl_FragColor = fColor;\n");
    ShadAppend("}");

    DBG(SHUT_LOGD("FPE Shader: \n%s\n", shad);)

    return (const char* const*)&shad;
}

#include <ctype.h>
#include "vgpu/shaderconv.h"

static int is_essl_3xx_source(const char* src) {
    if (!src) return 0;
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') ++src;
    return (strncmp(src, "#version 300 es", 15) == 0 ||
            strncmp(src, "#version 310 es", 15) == 0 ||
            strncmp(src, "#version 320 es", 15) == 0);
}

typedef enum {
    FRAG_COLOR0_NONE = 0,
    FRAG_COLOR0_LEGACY = 1,
    FRAG_COLOR0_ESSL3_OUT = 2
} frag_color0_kind_t;

typedef struct {
    frag_color0_kind_t kind;
    char expr[128];
    char ident[128];
    size_t id_offset;
    size_t id_len;
    size_t semi_offset;
    int arr_count;
} frag_color0_decl_t;

static void find_frag_color0_decl(const char* src, frag_color0_decl_t* out) {
    memset(out, 0, sizeof(*out));
    if (!src) return;
    if (gl4es_find_string(src, "gl_FragColor")) {
        out->kind = FRAG_COLOR0_LEGACY;
        snprintf(out->expr, sizeof(out->expr), "gl_FragColor");
        return;
    }
    if (gl4es_find_string(src, "gl4es_FragColor")) {
        out->kind = FRAG_COLOR0_LEGACY;
        snprintf(out->expr, sizeof(out->expr), "gl4es_FragColor");
        return;
    }
    if (strstr(src, "gl_FragData")) {
        out->kind = FRAG_COLOR0_LEGACY;
        snprintf(out->expr, sizeof(out->expr), "gl_FragData[0]");
        return;
    }

    /* Scan top-level global declarations (brace_depth == 0 && paren_depth == 0) for
       `layout(... location = 0 ...) out ... vec4 <name>` or implicit `out ... vec4 <name>` */
    frag_color0_decl_t fallback;
    memset(&fallback, 0, sizeof(fallback));
    int brace_depth = 0;
    int paren_depth = 0;
    const char* p = src;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            p += 2;
            while (*p && *p != '\n') ++p;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
            if (*p) p += 2;
            continue;
        }
        if (*p == '#') {
            while (*p && *p != '\n') ++p;
            continue;
        }
        if (*p == '{') { ++brace_depth; ++p; continue; }
        if (*p == '}') { if (brace_depth > 0) --brace_depth; ++p; continue; }
        if (*p == '(') { ++paren_depth; ++p; continue; }
        if (*p == ')') { if (paren_depth > 0) --paren_depth; ++p; continue; }
        if (brace_depth > 0 || paren_depth > 0) { ++p; continue; }

        int has_loc0 = 0;
        int has_other_loc = 0;
        if (strncmp(p, "layout", 6) == 0 && !isalnum((unsigned char)p[6]) && p[6] != '_' &&
            (p == src || (!isalnum((unsigned char)p[-1]) && p[-1] != '_'))) {
            const char* q = p + 6;
            while (*q && isspace((unsigned char)*q)) ++q;
            if (*q == '(') {
                const char* close = strchr(q + 1, ')');
                if (close) {
                    const char* loc = q + 1;
                    while (loc < close) {
                        const char* hit = strstr(loc, "location");
                        if (!hit || hit >= close) break;
                        const char* eq = hit + 8;
                        while (eq < close && isspace((unsigned char)*eq)) ++eq;
                        if (eq < close && *eq == '=') {
                            ++eq;
                            while (eq < close && isspace((unsigned char)*eq)) ++eq;
                            long val = strtol(eq, NULL, 10);
                            if (val == 0) has_loc0 = 1;
                            else has_other_loc = 1;
                        }
                        loc = hit + 8;
                    }
                    p = close + 1;
                    while (*p && isspace((unsigned char)*p)) ++p;
                }
            }
        }

        if (strncmp(p, "out", 3) == 0 && isspace((unsigned char)p[3]) &&
            (p == src || (!isalnum((unsigned char)p[-1]) && p[-1] != '_'))) {
            const char* q = p + 3;
            while (*q && *q != ';' && *q != '{') {
                while (*q && isspace((unsigned char)*q)) ++q;
                if (strncmp(q, "highp", 5) == 0 && isspace((unsigned char)q[5])) { q += 5; continue; }
                if (strncmp(q, "mediump", 7) == 0 && isspace((unsigned char)q[7])) { q += 7; continue; }
                if (strncmp(q, "lowp", 4) == 0 && isspace((unsigned char)q[4])) { q += 4; continue; }
                if (strncmp(q, "centroid", 8) == 0 && isspace((unsigned char)q[8])) { q += 8; continue; }
                if (strncmp(q, "smooth", 6) == 0 && isspace((unsigned char)q[6])) { q += 6; continue; }
                if (strncmp(q, "flat", 4) == 0 && isspace((unsigned char)q[4])) { q += 4; continue; }
                if (strncmp(q, "vec4", 4) == 0 && isspace((unsigned char)q[4])) {
                    q += 4;
                    while (*q && isspace((unsigned char)*q)) ++q;
                    const char* id0 = q;
                    while (*q && (isalnum((unsigned char)*q) || *q == '_')) ++q;
                    size_t idlen = (size_t)(q - id0);
                    const char* semi = strchr(q, ';');
                    if (idlen > 0 && semi && idlen + 4 < sizeof(out->ident)) {
                        const char* after = q;
                        while (after < semi && isspace((unsigned char)*after)) ++after;
                        int arr_cnt = 0;
                        if (after < semi && *after == '[') {
                            long n = strtol(after + 1, NULL, 10);
                            arr_cnt = (n > 0) ? (int)n : 1;
                        }
                        frag_color0_decl_t cand;
                        memset(&cand, 0, sizeof(cand));
                        cand.kind = FRAG_COLOR0_ESSL3_OUT;
                        memcpy(cand.ident, id0, idlen);
                        cand.ident[idlen] = '\0';
                        memcpy(cand.expr, id0, idlen);
                        if (arr_cnt > 0) memcpy(cand.expr + idlen, "[0]", 4);
                        else cand.expr[idlen] = '\0';
                        cand.id_offset = (size_t)(id0 - src);
                        cand.id_len = idlen;
                        cand.semi_offset = (size_t)(semi - src);
                        cand.arr_count = arr_cnt;
                        if (has_loc0) {
                            *out = cand;
                            return;
                        } else if (!has_other_loc && fallback.kind == FRAG_COLOR0_NONE) {
                            fallback = cand;
                        }
                    }
                }
                break;
            }
        }
        ++p;
    }
    if (fallback.kind != FRAG_COLOR0_NONE) {
        *out = fallback;
    }
}

/* ESSL 3.00 §4.3.6 forbids reading fragment `out` variables (enforced strictly by ARM Mali).
   Rewrite `out ... vec4 <ident>;` -> `out ... vec4 _emesis_FinalOut0;\nhighp vec4 <ident>;` so
   all reads/writes inside `_gl4es_main()` and alpha test operate on an ordinary global `vec4`,
   and `_emesis_FinalOut0` is written once at the end of `main()`. */
static int stage_essl3_frag_out0(const frag_color0_decl_t* decl) {
    if (!decl || decl->kind != FRAG_COLOR0_ESSL3_OUT || !shad) return 0;
    size_t src_len = strlen(shad);
    if (decl->semi_offset >= src_len || decl->id_offset + decl->id_len > decl->semi_offset) return 0;

    char repl[384];
    int has_comma = (memchr(shad + decl->id_offset + decl->id_len, ',',
                            decl->semi_offset - (decl->id_offset + decl->id_len)) != NULL);
    if (!has_comma) {
        if (decl->arr_count > 0) {
            snprintf(repl, sizeof(repl), "_emesis_FinalOut0[%d];\nhighp vec4 %s[%d];",
                     decl->arr_count, decl->ident, decl->arr_count);
        } else {
            snprintf(repl, sizeof(repl), "_emesis_FinalOut0;\nhighp vec4 %s;", decl->ident);
        }
        size_t old_span = (decl->semi_offset + 1) - decl->id_offset;
        size_t repl_len = strlen(repl);
        size_t needed = src_len - old_span + repl_len + 1;
        if ((int)needed > shad_cap) {
            int new_cap = shad_cap ? shad_cap : 1024;
            while (new_cap < (int)needed) new_cap *= 2;
            char* grown = (char*)realloc(shad, (size_t)new_cap);
            if (!grown) return 0;
            shad = grown;
            shad_cap = new_cap;
        }
        memmove(shad + decl->id_offset + repl_len,
                shad + decl->semi_offset + 1,
                src_len - (decl->semi_offset + 1) + 1);
        memcpy(shad + decl->id_offset, repl, repl_len);
        return 1;
    }

    const char* out_name = "_emesis_FinalOut0";
    size_t out_len = strlen(out_name);
    snprintf(repl, sizeof(repl), "\nhighp vec4 %s;", decl->ident);
    size_t extra_len = strlen(repl);
    size_t needed = src_len - decl->id_len + out_len + extra_len + 1;
    if ((int)needed > shad_cap) {
        int new_cap = shad_cap ? shad_cap : 1024;
        while (new_cap < (int)needed) new_cap *= 2;
        char* grown = (char*)realloc(shad, (size_t)new_cap);
        if (!grown) return 0;
        shad = grown;
        shad_cap = new_cap;
    }
    size_t tail_after_semi = src_len - (decl->semi_offset + 1) + 1;
    size_t mid_len = (decl->semi_offset + 1) - (decl->id_offset + decl->id_len);
    memmove(shad + decl->id_offset + out_len + mid_len + extra_len,
            shad + decl->semi_offset + 1,
            tail_after_semi);
    memmove(shad + decl->id_offset + out_len,
            shad + decl->id_offset + decl->id_len,
            mid_len);
    memcpy(shad + decl->id_offset, out_name, out_len);
    memcpy(shad + decl->id_offset + out_len + mid_len, repl, extra_len);
    return 1;
}

const char* const* fpe_CustomVertexShader(const char* initial, fpe_state_t* state, int default_fragment) {
    int planes = state->plane;
    char buff[1024];
    if (!shad_cap) shad_cap = 1024;
    if (!shad) shad = (char*)malloc(shad_cap);
    if (!initial) {
        shad[0] = '\0';
        return (const char* const*)&shad;
    }

    int color = default_fragment ? (strstr(initial, "_gl4es_Color") ? 0 : 1) : 0;
    strcpy(shad, "");
    ShadAppend(initial);
    if (!planes && !color) {
        return (const char* const*)&shad;
    }

    size_t cur_len = strlen(shad);
    if (cur_len > 0 && shad[cur_len - 1] != '\n') {
        ShadAppend("\n");
    }

    int essl3 = is_essl_3xx_source(initial);
    if (planes) {
        for (int i = 0; i < hardext.maxplanes; i++) {
            if ((planes >> i) & 1) {
                sprintf(buff, "uniform highp vec4 _gl4es_ClipPlane_%d;\n", i);
                ShadAppend(buff);
                sprintf(buff, "%s mediump float clippedvertex_%d;\n", essl3 ? "out" : "varying", i);
                ShadAppend(buff);
            }
        }
    }
    if (color) {
        sprintf(buff, "%s lowp vec4 _gl4es_Color;\n", essl3 ? "in" : "attribute");
        ShadAppend(buff);
        sprintf(buff, "%s lowp vec4 Color;\n", essl3 ? "out" : "varying");
        ShadAppend(buff);
    }

    shad = ReplaceFunctionName(shad, &shad_cap, "main", "_gl4es_main");

    if (strstr(shad, "_gl4es_main")) {
        ShadAppend("\nvoid main() {\n");
        if (color) {
            ShadAppend("Color = _gl4es_Color;\n");
        }
        ShadAppend("_gl4es_main();\n");
        if (planes) {
            const char* vexpr = "gl_ModelViewMatrix * gl_Vertex";
            if (strstr(shad, "gl4es_ClipVertex")) vexpr = "gl4es_ClipVertex";
            else if (strstr(shad, "_gl4es_ModelViewMatrix") && strstr(shad, "_gl4es_Vertex"))
                vexpr = "_gl4es_ModelViewMatrix * _gl4es_Vertex";
            for (int i = 0; i < hardext.maxplanes; i++) {
                if ((planes >> i) & 1) {
                    sprintf(buff, "clippedvertex_%d = dot(%s, _gl4es_ClipPlane_%d);\n", i, vexpr, i);
                    ShadAppend(buff);
                }
            }
        }
        ShadAppend("}\n");
    }

    return (const char* const*)&shad;
}

const char* const* fpe_CustomFragmentShader(const char* initial, fpe_state_t* state) {
    if (!shad_cap) shad_cap = 1024;
    if (!shad) shad = (char*)malloc(shad_cap);
    if (!initial) {
        shad[0] = '\0';
        return (const char* const*)&shad;
    }

    int planes = state->plane;
    int alpha_test = state->alphatest;
    int alpha_func = state->alphafunc;
    char buff[1024];

    strcpy(shad, "");
    ShadAppend(initial);
    if (!planes && (!alpha_test || alpha_func == FPE_ALWAYS)) {
        return (const char* const*)&shad;
    }

    frag_color0_decl_t color0_decl;
    memset(&color0_decl, 0, sizeof(color0_decl));
    int staged_essl3_out = 0;
    if (alpha_test && alpha_func > FPE_NEVER) {
        find_frag_color0_decl(shad, &color0_decl);
        /* Depth-only fragment shaders (e.g. shadow.fsh with no color output) have no alpha output to test */
        if (color0_decl.kind == FRAG_COLOR0_NONE && !planes) {
            return (const char* const*)&shad;
        }
        if (color0_decl.kind == FRAG_COLOR0_ESSL3_OUT) {
            staged_essl3_out = stage_essl3_frag_out0(&color0_decl);
        }
    }

    size_t cur_len = strlen(shad);
    if (cur_len > 0 && shad[cur_len - 1] != '\n') {
        ShadAppend("\n");
    }

    int essl3 = is_essl_3xx_source(initial);
    if (planes) {
        for (int i = 0; i < hardext.maxplanes; i++) {
            if ((planes >> i) & 1) {
                sprintf(buff, "%s mediump float clippedvertex_%d;\n", essl3 ? "in" : "varying", i);
                ShadAppend(buff);
            }
        }
    }
    if (alpha_test && alpha_func > FPE_NEVER && color0_decl.kind != FRAG_COLOR0_NONE) {
        ShadAppend("#ifdef GL_ES\nuniform highp float _gl4es_AlphaRef;\n#else\nuniform float _gl4es_AlphaRef;\n#endif\n");
    }

    shad = ReplaceFunctionName(shad, &shad_cap, "main", "_gl4es_main");

    if (strstr(shad, "_gl4es_main")) {
        ShadAppend("void main() {\n");
        if (staged_essl3_out) {
            if (color0_decl.arr_count > 0) {
                for (int ai = 0; ai < color0_decl.arr_count && ai < 16; ++ai) {
                    sprintf(buff, " %s[%d] = vec4(0.0);\n", color0_decl.ident, ai);
                    ShadAppend(buff);
                }
            } else {
                sprintf(buff, " %s = vec4(0.0);\n", color0_decl.ident);
                ShadAppend(buff);
            }
        }
        ShadAppend(" _gl4es_main();\n");
        if (planes) {
            ShadAppend(" if((");
            int k = 0;
            for (int i = 0; i < hardext.maxplanes; i++) {
                if ((planes >> i) & 1) {
                    sprintf(buff, "%smin(0., clippedvertex_%d)", k ? "+" : "", i);
                    ShadAppend(buff);
                    k = 1;
                }
            }
            ShadAppend(")<0.) discard;\n");
        }

        if (alpha_test) {
            if (comments) {
                sprintf(buff, "// Alpha Test, fct=%X\n", alpha_func);
                ShadAppend(buff);
            }
            if (alpha_func == FPE_ALWAYS) {
                // nothing here...
            } else if (alpha_func == FPE_NEVER) {
                ShadAppend(" discard;\n");
            } else if (color0_decl.kind != FRAG_COLOR0_NONE) {
                const char* alpha_test_op[] = {">=", "!=", ">", "<=", "==", "<"};
                sprintf(buff, " if (floor(%s.a*255.) %s _gl4es_AlphaRef) discard;\n",
                        color0_decl.expr, alpha_test_op[alpha_func - FPE_LESS]);
                ShadAppend(buff);
            }
        }

        if (staged_essl3_out) {
            if (color0_decl.arr_count > 0) {
                for (int ai = 0; ai < color0_decl.arr_count && ai < 16; ++ai) {
                    sprintf(buff, " _emesis_FinalOut0[%d] = %s[%d];\n", ai, color0_decl.ident, ai);
                    ShadAppend(buff);
                }
            } else {
                sprintf(buff, " _emesis_FinalOut0 = %s;\n", color0_decl.ident);
                ShadAppend(buff);
            }
        }

        ShadAppend("}\n");
    }

    return (const char* const*)&shad;
}

#ifdef GL4ES_COMPILE_FOR_USE_IN_SHARED_LIB
void fpe_shader_reset_internals() {
    if (shad) {
        free(shad);
        shad = NULL;
    }
    shad_cap = 0;
    comments = 1;
}
#endif
