#include "shadermod_uniforms.h"
#include "gl4es.h"
#include "glstate.h"
#include "program.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int g_emesis_internal_uniform_upload = 0;

typedef struct {
    int inited;
    uint32_t gen;
    uint32_t derived_gen;

    float view_width;
    float view_height;
    int frame_counter;
    float frame_time;
    float frame_time_counter;
    int world_time;
    float sun_angle;
    int has_sun_angle;

    float sun_pos[3];
    int has_sun_pos;
    float moon_pos[3];
    int has_moon_pos;
    float up_pos[3];
    int has_up_pos;
    float shadow_light_pos[3];
    int has_shadow_light_pos;
    float gbuffer_mv_inv[16];

    float eye_brightness[2];
    int has_eye_brightness;
    float rain_strength;
    float wetness;
    int is_eye_in_water;

    int taa_mode;

    /* Frame-coherent exponential smoothers */
    int smooth_inited;
    int last_smooth_frame;
    float last_smooth_ftc;
    float exp_smooth;
    float outdoors_smooth;
    float precip_smooth;
    float rainbow_smooth;

    /* Cached derived custom uniform values */
    float d_view_size[2];
    float d_pixel_size[2];
    float d_aspect_ratio;
    float d_framemod8_f;
    GLint d_framemod8_i;
    float d_taa_offset[2];
    float d_taa_jitter[2];
    float d_up_dir_view[3];
    float d_up_dir[3];
    float d_sun_dir_view[3];
    float d_sun_dir[3];
    float d_moon_dir_view[3];
    float d_moon_dir[3];
    float d_light_dir_view[3];
    float d_light_dir[3];
    float d_sun_or_moon_pos_n[3];
    float d_light_flip;
    float d_time_angle;
    float d_sun_angle_at_home;
    float d_day_strength;
    float d_night_strength;
    float d_sunrise_strength;
    float d_sunset_strength;
    float d_daytime[4];
    float d_exposure_level;
    float d_is_outdoors_smooth;
    float d_precipitation_smooth;
    float d_rainbow_strength;
    float d_fog_amount;
} emesis_sm_state_t;

static emesis_sm_state_t g_sm = {0};

static void sm_ensure_init(void) {
    if (g_sm.inited) return;
    memset(&g_sm, 0, sizeof(g_sm));
    g_sm.inited = 1;
    g_sm.gen = 1;
    g_sm.view_width = 1280.0f;
    g_sm.view_height = 720.0f;
    g_sm.frame_time = 1.0f / 60.0f;
    g_sm.world_time = 1000;
    g_sm.sun_angle = 0.08f;
    g_sm.sun_pos[0] = 0.0f;
    g_sm.sun_pos[1] = 100.0f;
    g_sm.sun_pos[2] = 0.0f;
    g_sm.moon_pos[0] = 0.0f;
    g_sm.moon_pos[1] = -100.0f;
    g_sm.moon_pos[2] = 0.0f;
    g_sm.up_pos[0] = 0.0f;
    g_sm.up_pos[1] = 100.0f;
    g_sm.up_pos[2] = 0.0f;
    g_sm.shadow_light_pos[0] = 0.0f;
    g_sm.shadow_light_pos[1] = 100.0f;
    g_sm.shadow_light_pos[2] = 0.0f;
    g_sm.gbuffer_mv_inv[0] = 1.0f;
    g_sm.gbuffer_mv_inv[5] = 1.0f;
    g_sm.gbuffer_mv_inv[10] = 1.0f;
    g_sm.gbuffer_mv_inv[15] = 1.0f;
    g_sm.eye_brightness[0] = 0.0f;
    g_sm.eye_brightness[1] = 240.0f;
    g_sm.exp_smooth = 1.0f;
    g_sm.outdoors_smooth = 1.0f;
}

static inline float sm_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline void sm_norm3(const float v[3], float out[3], float fb_y) {
    float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (len2 > 1e-12f) {
        float inv = 1.0f / sqrtf(len2);
        out[0] = v[0] * inv;
        out[1] = v[1] * inv;
        out[2] = v[2] * inv;
    } else {
        out[0] = 0.0f;
        out[1] = fb_y;
        out[2] = 0.0f;
    }
}

static inline void sm_mat3_mul_vec3(const float m16[16], const float v[3], float out[3]) {
    out[0] = m16[0] * v[0] + m16[4] * v[1] + m16[8] * v[2];
    out[1] = m16[1] * v[0] + m16[5] * v[1] + m16[9] * v[2];
    out[2] = m16[2] * v[0] + m16[6] * v[1] + m16[10] * v[2];
}

static inline float sm_step_smooth(float cur, float target, float dt, float fade_up, float fade_down) {
    float tau = (target >= cur) ? fade_up : fade_down;
    if (tau <= 1e-5f) return target;
    float alpha = 1.0f - expf(-dt / tau);
    alpha = sm_clampf(alpha, 0.0f, 1.0f);
    return cur + (target - cur) * alpha;
}

uint8_t emesis_sm_classify_source(const char* name) {
    if (!name || !name[0]) return EMESIS_SM_SRC_NONE;
    switch (name[0]) {
    case 'e':
        if (strcmp(name, "eyeBrightness") == 0) return EMESIS_SM_SRC_EYE_BRIGHTNESS;
        if (strcmp(name, "eyeBrightnessSmooth") == 0) return EMESIS_SM_SRC_EYE_BRIGHTNESS_SMOOTH;
        break;
    case 'f':
        if (strcmp(name, "frameCounter") == 0) return EMESIS_SM_SRC_FRAME_COUNTER;
        if (strcmp(name, "frameTime") == 0) return EMESIS_SM_SRC_FRAME_TIME;
        if (strcmp(name, "frameTimeCounter") == 0) return EMESIS_SM_SRC_FRAME_TIME_COUNTER;
        break;
    case 'g':
        if (strcmp(name, "gbufferModelViewInverse") == 0) return EMESIS_SM_SRC_GBUFFER_MV_INVERSE;
        break;
    case 'i':
        if (strcmp(name, "isEyeInWater") == 0) return EMESIS_SM_SRC_IS_EYE_IN_WATER;
        break;
    case 'm':
        if (strcmp(name, "moonPosition") == 0) return EMESIS_SM_SRC_MOON_POSITION;
        break;
    case 'r':
        if (strcmp(name, "rainStrength") == 0) return EMESIS_SM_SRC_RAIN_STRENGTH;
        break;
    case 's':
        if (strcmp(name, "sunAngle") == 0) return EMESIS_SM_SRC_SUN_ANGLE;
        if (strcmp(name, "sunPosition") == 0) return EMESIS_SM_SRC_SUN_POSITION;
        if (strcmp(name, "shadowLightPosition") == 0) return EMESIS_SM_SRC_SHADOW_LIGHT_POSITION;
        break;
    case 'u':
        if (strcmp(name, "upPosition") == 0) return EMESIS_SM_SRC_UP_POSITION;
        break;
    case 'v':
        if (strcmp(name, "viewWidth") == 0) return EMESIS_SM_SRC_VIEW_WIDTH;
        if (strcmp(name, "viewHeight") == 0) return EMESIS_SM_SRC_VIEW_HEIGHT;
        break;
    case 'w':
        if (strcmp(name, "worldTime") == 0) return EMESIS_SM_SRC_WORLD_TIME;
        if (strcmp(name, "wetness") == 0) return EMESIS_SM_SRC_WETNESS;
        break;
    default:
        break;
    }
    return EMESIS_SM_SRC_NONE;
}

uint8_t emesis_sm_classify_custom(const char* name, GLenum type) {
    if (!name || !name[0]) return EMESIS_SM_CUST_NONE;
    switch (name[0]) {
    case 'a':
        if (type == GL_FLOAT && strcmp(name, "aspectRatio") == 0) return EMESIS_SM_CUST_ASPECT_RATIO;
        break;
    case 'd':
        if (type == GL_FLOAT && strcmp(name, "dayStrength") == 0) return EMESIS_SM_CUST_DAY_STRENGTH;
        if (type == GL_FLOAT_VEC4 && strcmp(name, "daytime") == 0) return EMESIS_SM_CUST_DAYTIME_VEC4;
        break;
    case 'e':
        if (type == GL_FLOAT && strcmp(name, "exposureLevel") == 0) return EMESIS_SM_CUST_EXPOSURE_LEVEL;
        break;
    case 'f':
        if (type == GL_FLOAT && strcmp(name, "fogAmount") == 0) return EMESIS_SM_CUST_FOG_AMOUNT;
        if (strcmp(name, "framemod8") == 0) {
            if (type == GL_FLOAT) return EMESIS_SM_CUST_FRAMEMOD8_F;
            if (type == GL_INT) return EMESIS_SM_CUST_FRAMEMOD8_I;
        }
        break;
    case 'i':
        if (type == GL_FLOAT && strcmp(name, "isOutdoorsSmooth") == 0) return EMESIS_SM_CUST_IS_OUTDOORS_SMOOTH;
        break;
    case 'l':
        if (type == GL_FLOAT_VEC3 && strcmp(name, "lightDirView") == 0) return EMESIS_SM_CUST_LIGHT_DIR_VIEW;
        if (type == GL_FLOAT_VEC3 && strcmp(name, "lightDir") == 0) return EMESIS_SM_CUST_LIGHT_DIR;
        if (type == GL_FLOAT && strcmp(name, "lightFlip") == 0) return EMESIS_SM_CUST_LIGHT_FLIP;
        break;
    case 'm':
        if (type == GL_FLOAT_VEC3 && strcmp(name, "moonDirView") == 0) return EMESIS_SM_CUST_MOON_DIR_VIEW;
        if (type == GL_FLOAT_VEC3 && strcmp(name, "moonDir") == 0) return EMESIS_SM_CUST_MOON_DIR;
        break;
    case 'n':
        if (type == GL_FLOAT && strcmp(name, "nightStrength") == 0) return EMESIS_SM_CUST_NIGHT_STRENGTH;
        break;
    case 'p':
        if (type == GL_FLOAT_VEC2 && strcmp(name, "pixelSize") == 0) return EMESIS_SM_CUST_PIXEL_SIZE;
        if (type == GL_FLOAT && strcmp(name, "precipitationSmooth") == 0) return EMESIS_SM_CUST_PRECIPITATION_SMOOTH;
        break;
    case 'r':
        if (type == GL_FLOAT_VEC2 && strcmp(name, "resolution") == 0) return EMESIS_SM_CUST_VIEW_SIZE;
        if (type == GL_FLOAT_VEC2 && strcmp(name, "resolutionInv") == 0) return EMESIS_SM_CUST_PIXEL_SIZE;
        if (type == GL_FLOAT && strcmp(name, "rainbowStrength") == 0) return EMESIS_SM_CUST_RAINBOW_STRENGTH;
        break;
    case 's':
        if (type == GL_FLOAT_VEC3 && (strcmp(name, "sunDirView") == 0 || strcmp(name, "sunPosN") == 0))
            return EMESIS_SM_CUST_SUN_DIR_VIEW;
        if (type == GL_FLOAT_VEC3 && strcmp(name, "sunDir") == 0) return EMESIS_SM_CUST_SUN_DIR;
        if (type == GL_FLOAT_VEC3 && strcmp(name, "sunOrMoonPosN") == 0) return EMESIS_SM_CUST_SUN_OR_MOON_POS_N;
        if (type == GL_FLOAT && strcmp(name, "sunAngleAtHome") == 0) return EMESIS_SM_CUST_SUN_ANGLE_AT_HOME;
        if (type == GL_FLOAT && strcmp(name, "sunriseStrength") == 0) return EMESIS_SM_CUST_SUNRISE_STRENGTH;
        if (type == GL_FLOAT && strcmp(name, "sunsetStrength") == 0) return EMESIS_SM_CUST_SUNSET_STRENGTH;
        break;
    case 't':
        if (type == GL_FLOAT_VEC2 && strcmp(name, "taaOffset") == 0) return EMESIS_SM_CUST_TAA_OFFSET;
        if (type == GL_FLOAT_VEC2 && strcmp(name, "taaJitter") == 0) return EMESIS_SM_CUST_TAA_JITTER;
        if (type == GL_FLOAT_VEC2 && strcmp(name, "texelSize") == 0) return EMESIS_SM_CUST_PIXEL_SIZE;
        if (type == GL_FLOAT && strcmp(name, "timeAngle") == 0) return EMESIS_SM_CUST_TIME_ANGLE;
        break;
    case 'u':
        if (type == GL_FLOAT_VEC3 && strcmp(name, "upDirView") == 0) return EMESIS_SM_CUST_UP_DIR_VIEW;
        if (type == GL_FLOAT_VEC3 && strcmp(name, "upDir") == 0) return EMESIS_SM_CUST_UP_DIR;
        break;
    case 'v':
        if (type == GL_FLOAT_VEC2 && strcmp(name, "viewSize") == 0) return EMESIS_SM_CUST_VIEW_SIZE;
        break;
    default:
        break;
    }
    return EMESIS_SM_CUST_NONE;
}

void emesis_sm_scan_shader_defines(const char* glsl) {
    if (!glsl) return;
    const char* p = strstr(glsl, "TAA_MODE");
    while (p) {
        /* Match `#define TAA_MODE <int>` */
        const char* ls = p;
        while (ls > glsl && ls[-1] != '\n' && ls[-1] != '\r') --ls;
        while (*ls == ' ' || *ls == '\t') ++ls;
        if (*ls == '#') {
            ++ls;
            while (*ls == ' ' || *ls == '\t') ++ls;
            if (strncmp(ls, "define", 6) == 0 && (ls[6] == ' ' || ls[6] == '\t')) {
                const char* after = p + 8;
                if (*after == ' ' || *after == '\t') {
                    while (*after == ' ' || *after == '\t') ++after;
                    if (isdigit((unsigned char)*after) || *after == '-') {
                        sm_ensure_init();
                        int mode = atoi(after);
                        if (g_sm.taa_mode != mode) {
                            g_sm.taa_mode = mode;
                            g_sm.gen++;
                        }
                        return;
                    }
                }
            }
        }
        p = strstr(p + 8, "TAA_MODE");
    }
}

void emesis_sm_observe_float(uint8_t src_id, int size, int count, const GLfloat* value) {
    if (!src_id || count <= 0 || !value) return;
    sm_ensure_init();
    int changed = 0;
    switch (src_id) {
    case EMESIS_SM_SRC_VIEW_WIDTH:
        if (size == 1 && value[0] > 1.0f && g_sm.view_width != value[0]) {
            g_sm.view_width = value[0];
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_VIEW_HEIGHT:
        if (size == 1 && value[0] > 1.0f && g_sm.view_height != value[0]) {
            g_sm.view_height = value[0];
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_FRAME_TIME:
        if (size == 1 && value[0] >= 0.0f && g_sm.frame_time != value[0]) {
            g_sm.frame_time = value[0];
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_FRAME_TIME_COUNTER:
        if (size == 1 && g_sm.frame_time_counter != value[0]) {
            g_sm.frame_time_counter = value[0];
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_SUN_ANGLE:
        if (size == 1 && (!g_sm.has_sun_angle || g_sm.sun_angle != value[0])) {
            g_sm.sun_angle = value[0];
            g_sm.has_sun_angle = 1;
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_SUN_POSITION:
        if (size == 3 && (!g_sm.has_sun_pos || memcmp(g_sm.sun_pos, value, 3 * sizeof(float)) != 0)) {
            memcpy(g_sm.sun_pos, value, 3 * sizeof(float));
            g_sm.has_sun_pos = 1;
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_MOON_POSITION:
        if (size == 3 && (!g_sm.has_moon_pos || memcmp(g_sm.moon_pos, value, 3 * sizeof(float)) != 0)) {
            memcpy(g_sm.moon_pos, value, 3 * sizeof(float));
            g_sm.has_moon_pos = 1;
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_UP_POSITION:
        if (size == 3 && (!g_sm.has_up_pos || memcmp(g_sm.up_pos, value, 3 * sizeof(float)) != 0)) {
            memcpy(g_sm.up_pos, value, 3 * sizeof(float));
            g_sm.has_up_pos = 1;
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_SHADOW_LIGHT_POSITION:
        if (size == 3 && (!g_sm.has_shadow_light_pos || memcmp(g_sm.shadow_light_pos, value, 3 * sizeof(float)) != 0)) {
            memcpy(g_sm.shadow_light_pos, value, 3 * sizeof(float));
            g_sm.has_shadow_light_pos = 1;
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_EYE_BRIGHTNESS_SMOOTH:
        if (size == 2 && !g_sm.has_eye_brightness) {
            g_sm.eye_brightness[0] = value[0];
            g_sm.eye_brightness[1] = value[1];
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_RAIN_STRENGTH:
        if (size == 1 && g_sm.rain_strength != value[0]) {
            g_sm.rain_strength = value[0];
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_WETNESS:
        if (size == 1 && g_sm.wetness != value[0]) {
            g_sm.wetness = value[0];
            changed = 1;
        }
        break;
    default:
        break;
    }
    if (changed) g_sm.gen++;
}

void emesis_sm_observe_int(uint8_t src_id, int size, int count, const GLint* value) {
    if (!src_id || count <= 0 || !value) return;
    sm_ensure_init();
    int changed = 0;
    switch (src_id) {
    case EMESIS_SM_SRC_FRAME_COUNTER:
        if (size == 1 && g_sm.frame_counter != value[0]) {
            g_sm.frame_counter = value[0];
            changed = 1;
        }
        break;
    case EMESIS_SM_SRC_WORLD_TIME:
        if (size == 1) {
            int wt = value[0] % 24000;
            if (wt < 0) wt += 24000;
            if (g_sm.world_time != wt) {
                g_sm.world_time = wt;
                changed = 1;
            }
        }
        break;
    case EMESIS_SM_SRC_EYE_BRIGHTNESS:
    case EMESIS_SM_SRC_EYE_BRIGHTNESS_SMOOTH:
        if (size == 2) {
            float e0 = (float)value[0];
            float e1 = (float)value[1];
            if (!g_sm.has_eye_brightness || g_sm.eye_brightness[0] != e0 || g_sm.eye_brightness[1] != e1) {
                g_sm.eye_brightness[0] = e0;
                g_sm.eye_brightness[1] = e1;
                g_sm.has_eye_brightness = 1;
                changed = 1;
            }
        }
        break;
    case EMESIS_SM_SRC_IS_EYE_IN_WATER:
        if (size == 1 && g_sm.is_eye_in_water != value[0]) {
            g_sm.is_eye_in_water = value[0];
            changed = 1;
        }
        break;
    default:
        break;
    }
    if (changed) g_sm.gen++;
}

void emesis_sm_observe_mat4(uint8_t src_id, int count, const GLfloat* value) {
    if (src_id != EMESIS_SM_SRC_GBUFFER_MV_INVERSE || count <= 0 || !value) return;
    sm_ensure_init();
    if (memcmp(g_sm.gbuffer_mv_inv, value, 16 * sizeof(float)) != 0) {
        memcpy(g_sm.gbuffer_mv_inv, value, 16 * sizeof(float));
        g_sm.gen++;
    }
}

static void sm_recompute_derived(void) {
    sm_ensure_init();

    float vw = g_sm.view_width;
    float vh = g_sm.view_height;
    if ((vw <= 1.0f || vh <= 1.0f) && glstate) {
        if (glstate->vp[2] > 1 && glstate->vp[3] > 1) {
            vw = (float)glstate->vp[2];
            vh = (float)glstate->vp[3];
        }
    }
    if (vw <= 1.0f) vw = 1280.0f;
    if (vh <= 1.0f) vh = 720.0f;

    g_sm.d_view_size[0] = vw;
    g_sm.d_view_size[1] = vh;
    g_sm.d_pixel_size[0] = 1.0f / vw;
    g_sm.d_pixel_size[1] = 1.0f / vh;
    g_sm.d_aspect_ratio = vw / vh;

    int fc = g_sm.frame_counter;
    if (fc < 0) fc = 0;
    int fmod8 = fc & 7;
    g_sm.d_framemod8_i = (GLint)fmod8;
    g_sm.d_framemod8_f = (float)fmod8;

    /* PotatoShaders Halton-8 * 0.6 / viewSize */
    static const float k_potato_jx[8] = {0.125f, -0.125f, 0.625f, 0.375f, -0.625f, -0.875f, 0.375f, 0.875f};
    static const float k_potato_jy[8] = {-0.375f, 0.375f, 0.125f, -0.625f, 0.625f, -0.125f, -0.875f, 0.875f};
    g_sm.d_taa_offset[0] = (k_potato_jx[fmod8] * 0.6f) / vw;
    g_sm.d_taa_offset[1] = (k_potato_jy[fmod8] * 0.6f) / vh;

    /* Mellow Shader taaJitter */
    float mjx = 0.0f, mjy = 0.0f;
    if (g_sm.taa_mode == 4) {
        mjx = ((fc & 1) == 0) ? 0.25f : -0.25f;
        mjy = -mjx;
    } else if (g_sm.taa_mode > 2) {
        const double gr = 1.32471795724474602596;
        double fx = 0.5 + (double)fc / gr;
        double fy = 0.5 + (double)fc / (gr * gr);
        fx -= floor(fx);
        fy -= floor(fy);
        mjx = (float)(fx * 2.0 - 1.0);
        mjy = (float)(fy * 2.0 - 1.0);
    }
    g_sm.d_taa_jitter[0] = mjx / vw;
    g_sm.d_taa_jitter[1] = mjy / vh;

    /* Time & Sun angles */
    float wt = (float)g_sm.world_time;
    float time_angle = wt / 24000.0f;
    g_sm.d_time_angle = time_angle;

    float d = time_angle - 0.25f;
    float e = 0.5f - cosf(d * 3.1415f) * 0.5f;
    float time_thing = (d * 2.0f + e) / 3.0f;
    float sun_angle_home = (time_thing < 0.75f) ? (time_thing + 0.25f) : (time_thing - 0.75f);
    g_sm.d_sun_angle_at_home = sun_angle_home;

    /* Mellow day/night/sunrise/sunset strengths */
    const float k_two_pi = 6.283185307179586f;
    float s_abs = fabsf(sinf(time_angle * k_two_pi)) * 2.0f;
    if (s_abs > 1.0f) s_abs = 1.0f;
    float is_night_half = (g_sm.world_time >= 12000) ? 1.0f : 0.0f;
    float night_s = s_abs * is_night_half;
    float day_s = s_abs * (1.0f - is_night_half);
    float rem = 1.0f - night_s - day_s;
    if (rem < 0.0f) rem = 0.0f;
    float is_sunset_sector = (((g_sm.world_time / 8000) % 2) != 0) ? 1.0f : 0.0f;
    float sunset_s = rem * is_sunset_sector;
    float ceil_sunset = (sunset_s > 0.0f) ? 1.0f : 0.0f;
    float sunrise_s = rem * (1.0f - ceil_sunset);
    g_sm.d_night_strength = night_s;
    g_sm.d_day_strength = day_s;
    g_sm.d_sunset_strength = sunset_s;
    g_sm.d_sunrise_strength = sunrise_s;

    /* Vectors: up, sun, moon, shadowLight */
    sm_norm3(g_sm.up_pos, g_sm.d_up_dir_view, 1.0f);
    g_sm.d_up_dir[0] = 0.0f;
    g_sm.d_up_dir[1] = 1.0f;
    g_sm.d_up_dir[2] = 0.0f;

    float sun_raw[3];
    float moon_raw[3];
    if (g_sm.has_sun_pos) {
        memcpy(sun_raw, g_sm.sun_pos, sizeof(sun_raw));
    } else {
        float ang = (g_sm.has_sun_angle ? g_sm.sun_angle : sun_angle_home) * k_two_pi;
        sun_raw[0] = -sinf(ang) * 100.0f;
        sun_raw[1] = cosf(ang) * 100.0f;
        sun_raw[2] = 0.0f;
    }
    if (g_sm.has_moon_pos) {
        memcpy(moon_raw, g_sm.moon_pos, sizeof(moon_raw));
    } else {
        moon_raw[0] = -sun_raw[0];
        moon_raw[1] = -sun_raw[1];
        moon_raw[2] = -sun_raw[2];
    }

    sm_norm3(sun_raw, g_sm.d_sun_dir_view, 1.0f);
    sm_mat3_mul_vec3(g_sm.gbuffer_mv_inv, g_sm.d_sun_dir_view, g_sm.d_sun_dir);
    sm_norm3(g_sm.d_sun_dir, g_sm.d_sun_dir, 1.0f);

    sm_norm3(moon_raw, g_sm.d_moon_dir_view, -1.0f);
    sm_mat3_mul_vec3(g_sm.gbuffer_mv_inv, g_sm.d_moon_dir_view, g_sm.d_moon_dir);
    sm_norm3(g_sm.d_moon_dir, g_sm.d_moon_dir, -1.0f);

    const float* sm_pick = (sun_angle_home < 0.5f) ? sun_raw : moon_raw;
    sm_norm3(sm_pick, g_sm.d_sun_or_moon_pos_n, 1.0f);

    const float* light_raw = g_sm.has_shadow_light_pos ? g_sm.shadow_light_pos : sm_pick;
    sm_norm3(light_raw, g_sm.d_light_dir_view, 1.0f);
    sm_mat3_mul_vec3(g_sm.gbuffer_mv_inv, g_sm.d_light_dir_view, g_sm.d_light_dir);
    sm_norm3(g_sm.d_light_dir, g_sm.d_light_dir, 1.0f);

    float sun_y = g_sm.d_sun_dir[1];
    float lf_a = 1.0f - ((sm_clampf(-sun_y, 0.002f, 0.04f) - 0.002f) / 0.038f);
    float lf_b = 1.0f - ((sm_clampf(sun_y, 0.002f, 0.04f) - 0.002f) / 0.038f);
    g_sm.d_light_flip = sm_clampf(1.0f - (lf_a * lf_b), 0.0f, 1.0f);

    /* Potato daytime & exposure */
    float t_sunrise = ((sm_clampf(wt, 22500.0f, 23500.0f) - 22500.0f) / 1000.0f) +
                      (1.0f - (sm_clampf(wt, 0.0f, 1500.0f) / 1500.0f));
    float t_noon = (sm_clampf(wt, 0.0f, 1500.0f) / 1500.0f) -
                   ((sm_clampf(wt, 10500.0f, 12000.0f) - 10500.0f) / 1500.0f);
    float t_sunset = ((sm_clampf(wt, 10500.0f, 12000.0f) - 10500.0f) / 1500.0f) -
                     ((sm_clampf(wt, 12500.0f, 13500.0f) - 12500.0f) / 1000.0f);
    float t_night = ((sm_clampf(wt, 12500.0f, 13500.0f) - 12500.0f) / 1000.0f) -
                    ((sm_clampf(wt, 22500.0f, 23500.0f) - 22500.0f) / 1000.0f);
    t_sunrise = sm_clampf(t_sunrise, 0.0f, 1.0f);
    t_noon = sm_clampf(t_noon, 0.0f, 1.0f);
    t_sunset = sm_clampf(t_sunset, 0.0f, 1.0f);
    t_night = sm_clampf(t_night, 0.0f, 1.0f);

    g_sm.d_daytime[0] = t_sunrise * t_sunrise;
    g_sm.d_daytime[1] = sqrtf(t_noon);
    g_sm.d_daytime[2] = t_sunset * t_sunset;
    g_sm.d_daytime[3] = sqrtf(t_night);

    float outdoor_level = 0.7f * t_sunrise + 1.0f * t_noon + 0.7f * t_sunset + 0.5f * t_night;
    float eb_x = sm_clampf(g_sm.eye_brightness[0] / 240.0f, 0.0f, 1.0f);
    float eb_y = sm_clampf(g_sm.eye_brightness[1] / 240.0f, 0.0f, 1.0f);
    float outdoor_exp = eb_y * eb_y * eb_y;
    float indoor_exp = eb_x * eb_x;
    float exp_mix = 0.15f * (1.0f - outdoor_exp) + outdoor_level * outdoor_exp;
    float exp_target = (indoor_exp > exp_mix) ? indoor_exp : exp_mix;
    if (exp_target < 0.05f) exp_target = 0.05f;

    float outdoors_target = (g_sm.eye_brightness[1] > 50.0f) ? 1.0f : 0.0f;
    float precip_target = (g_sm.rain_strength > 0.01f) ? 1.0f : 0.0f;
    float rainbow_target = (g_sm.rain_strength + 0.1f < g_sm.wetness) ? 1.0f : 0.0f;

    if (!g_sm.smooth_inited) {
        g_sm.smooth_inited = 1;
        g_sm.last_smooth_frame = g_sm.frame_counter;
        g_sm.last_smooth_ftc = g_sm.frame_time_counter;
        g_sm.exp_smooth = exp_target;
        g_sm.outdoors_smooth = outdoors_target;
        g_sm.precip_smooth = precip_target;
        g_sm.rainbow_smooth = rainbow_target;
    } else if (g_sm.frame_counter != g_sm.last_smooth_frame ||
               fabsf(g_sm.frame_time_counter - g_sm.last_smooth_ftc) > 1e-4f) {
        float dt = g_sm.frame_time;
        if (dt <= 0.0f || dt > 0.25f) {
            float d_ftc = g_sm.frame_time_counter - g_sm.last_smooth_ftc;
            dt = (d_ftc > 0.0f && d_ftc <= 0.25f) ? d_ftc : (1.0f / 60.0f);
        }
        g_sm.last_smooth_frame = g_sm.frame_counter;
        g_sm.last_smooth_ftc = g_sm.frame_time_counter;
        g_sm.exp_smooth = sm_step_smooth(g_sm.exp_smooth, exp_target, dt, 4.0f, 3.0f);
        g_sm.outdoors_smooth = sm_step_smooth(g_sm.outdoors_smooth, outdoors_target, dt, 10.0f, 10.0f);
        g_sm.precip_smooth = sm_step_smooth(g_sm.precip_smooth, precip_target, dt, 150.0f, 150.0f);
        g_sm.rainbow_smooth = sm_step_smooth(g_sm.rainbow_smooth, rainbow_target, dt, 30.0f, 30.0f);
    }

    g_sm.d_exposure_level = 1.0f / sm_clampf(g_sm.exp_smooth, 0.05f, 10.0f);
    g_sm.d_is_outdoors_smooth = g_sm.outdoors_smooth;
    g_sm.d_precipitation_smooth = g_sm.precip_smooth;
    g_sm.d_rainbow_strength = g_sm.rainbow_smooth;

    float water_gate = (g_sm.is_eye_in_water == 1) ? 1.0f : g_sm.outdoors_smooth;
    g_sm.d_fog_amount = (sunrise_s + night_s + 1.0f + g_sm.rain_strength + (float)g_sm.is_eye_in_water * 30.0f) * water_gate;

    g_sm.derived_gen = g_sm.gen;
}

void emesis_sm_apply_program_custom_uniforms(void* glprogram_ptr) {
    program_t* glprogram = (program_t*)glprogram_ptr;
    if (!glprogram || glprogram->custom_uniform_count <= 0 || !glprogram->custom_uniforms) return;

    sm_ensure_init();
    if (glprogram->synced_sm_gen == g_sm.gen) return;
    int any_unset = 0;
    for (int i = 0; i < glprogram->custom_uniform_count; ++i) {
        uniform_t* m = glprogram->custom_uniforms[i];
        if (m && !m->app_set) {
            any_unset = 1;
            break;
        }
    }
    if (!any_unset) {
        glprogram->synced_sm_gen = g_sm.gen;
        return;
    }
    if (g_sm.derived_gen != g_sm.gen) {
        sm_recompute_derived();
    }

    int prev_internal = g_emesis_internal_uniform_upload;
    g_emesis_internal_uniform_upload = 1;

    for (int i = 0; i < glprogram->custom_uniform_count; ++i) {
        uniform_t* m = glprogram->custom_uniforms[i];
        if (!m || m->app_set) continue;
        switch (m->sm_custom_id) {
        case EMESIS_SM_CUST_VIEW_SIZE:
            GoUniformfv(glprogram, m->id, 2, 1, g_sm.d_view_size);
            break;
        case EMESIS_SM_CUST_PIXEL_SIZE:
            GoUniformfv(glprogram, m->id, 2, 1, g_sm.d_pixel_size);
            break;
        case EMESIS_SM_CUST_ASPECT_RATIO:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_aspect_ratio);
            break;
        case EMESIS_SM_CUST_FRAMEMOD8_F:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_framemod8_f);
            break;
        case EMESIS_SM_CUST_FRAMEMOD8_I:
            GoUniformiv(glprogram, m->id, 1, 1, &g_sm.d_framemod8_i);
            break;
        case EMESIS_SM_CUST_TAA_OFFSET:
            GoUniformfv(glprogram, m->id, 2, 1, g_sm.d_taa_offset);
            break;
        case EMESIS_SM_CUST_TAA_JITTER:
            GoUniformfv(glprogram, m->id, 2, 1, g_sm.d_taa_jitter);
            break;
        case EMESIS_SM_CUST_UP_DIR_VIEW:
            GoUniformfv(glprogram, m->id, 3, 1, g_sm.d_up_dir_view);
            break;
        case EMESIS_SM_CUST_UP_DIR:
            GoUniformfv(glprogram, m->id, 3, 1, g_sm.d_up_dir);
            break;
        case EMESIS_SM_CUST_SUN_DIR_VIEW:
            GoUniformfv(glprogram, m->id, 3, 1, g_sm.d_sun_dir_view);
            break;
        case EMESIS_SM_CUST_SUN_DIR:
            GoUniformfv(glprogram, m->id, 3, 1, g_sm.d_sun_dir);
            break;
        case EMESIS_SM_CUST_MOON_DIR_VIEW:
            GoUniformfv(glprogram, m->id, 3, 1, g_sm.d_moon_dir_view);
            break;
        case EMESIS_SM_CUST_MOON_DIR:
            GoUniformfv(glprogram, m->id, 3, 1, g_sm.d_moon_dir);
            break;
        case EMESIS_SM_CUST_LIGHT_DIR_VIEW:
            GoUniformfv(glprogram, m->id, 3, 1, g_sm.d_light_dir_view);
            break;
        case EMESIS_SM_CUST_LIGHT_DIR:
            GoUniformfv(glprogram, m->id, 3, 1, g_sm.d_light_dir);
            break;
        case EMESIS_SM_CUST_SUN_OR_MOON_POS_N:
            GoUniformfv(glprogram, m->id, 3, 1, g_sm.d_sun_or_moon_pos_n);
            break;
        case EMESIS_SM_CUST_LIGHT_FLIP:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_light_flip);
            break;
        case EMESIS_SM_CUST_TIME_ANGLE:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_time_angle);
            break;
        case EMESIS_SM_CUST_SUN_ANGLE_AT_HOME:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_sun_angle_at_home);
            break;
        case EMESIS_SM_CUST_DAY_STRENGTH:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_day_strength);
            break;
        case EMESIS_SM_CUST_NIGHT_STRENGTH:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_night_strength);
            break;
        case EMESIS_SM_CUST_SUNRISE_STRENGTH:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_sunrise_strength);
            break;
        case EMESIS_SM_CUST_SUNSET_STRENGTH:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_sunset_strength);
            break;
        case EMESIS_SM_CUST_DAYTIME_VEC4:
            GoUniformfv(glprogram, m->id, 4, 1, g_sm.d_daytime);
            break;
        case EMESIS_SM_CUST_EXPOSURE_LEVEL:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_exposure_level);
            break;
        case EMESIS_SM_CUST_IS_OUTDOORS_SMOOTH:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_is_outdoors_smooth);
            break;
        case EMESIS_SM_CUST_PRECIPITATION_SMOOTH:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_precipitation_smooth);
            break;
        case EMESIS_SM_CUST_RAINBOW_STRENGTH:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_rainbow_strength);
            break;
        case EMESIS_SM_CUST_FOG_AMOUNT:
            GoUniformfv(glprogram, m->id, 1, 1, &g_sm.d_fog_amount);
            break;
        default:
            break;
        }
    }

    g_emesis_internal_uniform_upload = prev_internal;
    glprogram->synced_sm_gen = g_sm.gen;
}
