/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include "egl.h"
#include "unordered_map/int_hash.h"
#include "string_utils.h"
#include "env.h"
#include <string.h>

thread_local context_t *current_context;
unordered_map* context_map;

EGLContext (*host_eglCreateContext)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
EGLBoolean (*host_eglDestroyContext)(EGLDisplay dpy, EGLContext ctx);
EGLBoolean (*host_eglMakeCurrent) (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLContext (*host_eglGetCurrentContext)(void);

void init_egl() {
    context_map = alloc_intmap();
    host_eglCreateContext = (EGLContext (*)(EGLDisplay, EGLConfig, EGLContext,
                                            const EGLint *)) resolve_egl_symbol("eglCreateContext");
    host_eglDestroyContext = (EGLBoolean (*)(EGLDisplay, EGLContext)) resolve_egl_symbol(
            "eglDestroyContext");
    host_eglMakeCurrent = (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface,
                                          EGLContext)) resolve_egl_symbol("eglMakeCurrent");
    host_eglGetCurrentContext = (EGLContext (*)(void)) resolve_egl_symbol("eglGetCurrentContext");
}

static bool init_context(context_t* tw_context) {
    tw_context->shader_map = alloc_intmap_safe();
    if(!tw_context->shader_map) goto fail;
    tw_context->framebuffer_map = alloc_intmap_safe();
    if(!tw_context->framebuffer_map) goto fail_dealloc;
    tw_context->program_map = alloc_intmap_safe();
    if(!tw_context->program_map) goto fail_dealloc;
    tw_context->texture_swztrack_map = alloc_intmap_safe();
    if(!tw_context->texture_swztrack_map) goto fail_dealloc;
    tw_context->vao_ebo_map = alloc_intmap_safe();
    if(!tw_context->vao_ebo_map) goto fail_dealloc;
    return true;

    fail_dealloc:
    if(tw_context->vao_ebo_map)
        unordered_map_free(tw_context->vao_ebo_map);
    if(tw_context->shader_map)
        unordered_map_free(tw_context->shader_map);
    if(tw_context->framebuffer_map)
        unordered_map_free(tw_context->framebuffer_map);
    if(tw_context->program_map)
        unordered_map_free(tw_context->program_map);
    if(tw_context->texture_swztrack_map)
        unordered_map_free(tw_context->texture_swztrack_map);
    fail:
    return false;
}

static void free_context(context_t* tw_context) {
    if(tw_context->shader_map) unordered_map_free(tw_context->shader_map);
    if(tw_context->program_map) unordered_map_free(tw_context->program_map);
    if(tw_context->framebuffer_map) unordered_map_free(tw_context->framebuffer_map);
    if(tw_context->texture_swztrack_map) unordered_map_free(tw_context->texture_swztrack_map);
    if(tw_context->vao_ebo_map) unordered_map_free(tw_context->vao_ebo_map);
    if(tw_context->basevertex.scratchPasses) free(tw_context->basevertex.scratchPasses);
    if(tw_context->basevertex.es30CpuScratch) free(tw_context->basevertex.es30CpuScratch);
    if(tw_context->extensions_string != NULL) free(tw_context->extensions_string);
    if(tw_context->nextras != 0 && tw_context->extra_extensions_array != NULL) {
        for(size_t i = 0; i < tw_context->nextras; i++) {
            free((tw_context->extra_extensions_array[i]));
        }
        free(tw_context->extra_extensions_array);
    }
}

void init_extra_extensions(context_t* context, int* length) {
    const char* es_extensions = (const char*)es3_functions.glGetString(GL_EXTENSIONS);
    if (!es_extensions) es_extensions = "";
    *length = (int)strlen(es_extensions);
    context->extensions_string = malloc(*length + 1);
    memcpy(context->extensions_string, es_extensions, *length+1);
}

void add_extra_extension(context_t* context, int* length, const char* extension)  {
    size_t extension_len = strlen(extension);

    char str_append_extension[extension_len + 2];
    memcpy(str_append_extension, extension, extension_len);
    str_append_extension[extension_len] = ' ';
    str_append_extension[extension_len + 1] = 0;
    context->extensions_string = gl4es_append(context->extensions_string, length, str_append_extension);

    int extension_idx = context->nextras++;
    context->extra_extensions_array = realloc(context->extra_extensions_array, sizeof(char*)*context->nextras);
    char* extra_extension = malloc(extension_len + 1);
    strncpy(extra_extension, extension, extension_len + 1);
    context->extra_extensions_array[extension_idx] = extra_extension;
}

void fin_extra_extensions(context_t* context, int length) {
    if(length < 2 || context->extensions_string == NULL) return;
    if(context->extensions_string[length-2] != ' ') return;
    char* orig_string = context->extensions_string;
    context->extensions_string = realloc(context->extensions_string, length - 1);
    if(context->extensions_string == NULL) {
        free(orig_string);
        return;
    }
    context->extensions_string[length-2] = 0;
}

static bool should_hide_buffer_storage(void) {
    if (sizeof(void*) == 4) {
        if (env_istrue("EMESIS_FORCE_BUFFER_STORAGE")) return false;
        return env_istrue_d("EMESIS_HIDE_BUFFER_STORAGE", true);
    }
    return env_istrue("EMESIS_HIDE_BUFFER_STORAGE");
}

void build_extension_string(context_t* context) {
    int length;
    init_extra_extensions(context, &length);
    if(context->buffer_storage) {
        if(!should_hide_buffer_storage())
            add_extra_extension(context, &length, "GL_ARB_buffer_storage");
        else
            printf("EmesisRenderer: Hiding GL_ARB_buffer_storage (%zu-bit address space protection)\n", sizeof(void*) * 8);
    }
    if(context->buffer_texture_ext || context->buffer_texture_oes || context->es32) {
        add_extra_extension(context, &length, "GL_ARB_texture_buffer_object");
    }
    add_extra_extension(context, &length, "GL_ARB_draw_elements_base_vertex");
    if(context->blending.available)
        add_extra_extension(context, &length, "GL_ARB_draw_buffers_blend");
    add_extra_extension(context, &length, "GL_ARB_clear_texture");
    // Used by Minecraft for the GPU usage counter (see Blaze3D TimerQuery; faked 1ns when hardware ext absent)
    add_extra_extension(context, &length, "GL_ARB_timer_query");
    if(context->es31) {
        add_extra_extension(context, &length, "GL_ARB_draw_indirect");
        add_extra_extension(context, &length, "GL_ARB_multi_draw_indirect");
    }
    if(context->base_instance) {
        add_extra_extension(context, &length, "GL_ARB_base_instance");
    }
    if(context->es31) {
        add_extra_extension(context, &length, "GL_ARB_shader_image_load_store");
        add_extra_extension(context, &length, "GL_ARB_shader_storage_buffer_object");
        add_extra_extension(context, &length, "GL_ARB_compute_shader");
    }
    fin_extra_extensions(context, length);
}

static void find_esversion(context_t* context) {
    const char* vendor = (const char*) es3_functions.glGetString(GL_VENDOR);
    const char* renderer = (const char*) es3_functions.glGetString(GL_RENDERER);
    const char* version = (const char*) es3_functions.glGetString(GL_VERSION);
    const char* shader_version = (const char*) es3_functions.glGetString(GL_SHADING_LANGUAGE_VERSION);

    snprintf(context->gpu_vendor, sizeof(context->gpu_vendor), "%s", vendor ? vendor : "Unknown");
    snprintf(context->gpu_renderer, sizeof(context->gpu_renderer), "%s", renderer ? renderer : "Unknown");
    snprintf(context->gpu_version, sizeof(context->gpu_version), "%s", version ? version : "Unknown");

    int esmajor = 0, esminor = 0, shadermajor = 3, shaderminor = 0;
    if (version) {
        const char* p = strstr(version, "OpenGL ES");
        if (p) {
            if (sscanf(p, "OpenGL ES %i.%i", &esmajor, &esminor) < 2) {
                sscanf(p, "OpenGL ES-%*[^ ] %i.%i", &esmajor, &esminor);
            }
        }
    }
    if (shader_version) {
        const char* sp = strstr(shader_version, "GLSL ES");
        char minor_str[8] = {0};
        if (sp && sscanf(sp, "GLSL ES %i.%7[0-9]", &shadermajor, minor_str) == 2) {
            shaderminor = atoi(minor_str);
            if (strlen(minor_str) == 1) shaderminor *= 10;
        }
    }
    context->shader_version = shadermajor * 100 + shaderminor;
    printf("EmesisRenderer: Running on OpenGL ES %i.%i with ESSL %i (%s)\n",
           esmajor, esminor, context->shader_version, context->gpu_renderer);
    if(esmajor == 0 && esminor == 0) goto fail;
    if(esmajor < 3 || context->shader_version < 300) {
        printf("Unsupported OpenGL ES version. This will cause you problems down the line.\n");
        return;
    }
    if(esmajor == 3) {
        context->es31 = esminor >= 1;
        context->es32 = esminor >= 2;
    }else if(esmajor > 3) {
        context->es32 = context->es31 = true;
    }

    const char* extensions = (const char*) es3_functions.glGetString(GL_EXTENSIONS);
    if (!extensions) extensions = "";
    if(strstr(extensions, "GL_EXT_buffer_storage")) context->buffer_storage = true;
    if(strstr(extensions, "GL_EXT_texture_buffer")) context->buffer_texture_ext = true;
    if(strstr(extensions, "GL_OES_texture_buffer")) context->buffer_texture_oes = true;
    if(strstr(extensions, "GL_EXT_multi_draw_indirect")) context->multidraw_indirect = true;
    if(strstr(extensions, "GL_EXT_base_instance")) context->base_instance = true;
    if(strstr(extensions, "GL_EXT_clear_texture")) context->clear_texture = true;
    if(strstr(extensions, "GL_EXT_disjoint_timer_query") || env_istrue_d("EMESIS_ENABLE_TIMER_QUERY", false)) context->timer_query = true;

    bool basevertex_oes = strstr(extensions, "GL_OES_draw_elements_base_vertex") != NULL;
    bool basevertex_ext = strstr(extensions, "GL_EXT_draw_elements_base_vertex") != NULL;
    bool multidraw_ext = strstr(extensions, "GL_EXT_multi_draw_arrays") != NULL;
    if(context->es32 && es3_functions.glDrawElementsBaseVertex)
        context->drawelementsbasevertex = es3_functions.glDrawElementsBaseVertex;
    else if(basevertex_oes && es3_functions.glDrawElementsBaseVertexOES)
        context->drawelementsbasevertex = es3_functions.glDrawElementsBaseVertexOES;
    else if(basevertex_ext && es3_functions.glDrawElementsBaseVertexEXT)
        context->drawelementsbasevertex = es3_functions.glDrawElementsBaseVertexEXT;
    else
        context->drawelementsbasevertex = NULL;

    context->multidrawelementsbasevertex = NULL;
    if (multidraw_ext && (context->es32 || basevertex_oes || basevertex_ext)) {
        if (es3_functions.glMultiDrawElementsBaseVertexEXT)
            context->multidrawelementsbasevertex = es3_functions.glMultiDrawElementsBaseVertexEXT;
        else if (es3_functions.glMultiDrawElementsBaseVertexOES)
            context->multidrawelementsbasevertex = es3_functions.glMultiDrawElementsBaseVertexOES;
    }

    bool drawbuffersi_oes = strstr(extensions, "GL_OES_draw_buffers_indexed") != NULL;
    bool drawbuffersi_ext = strstr(extensions, "GL_EXT_draw_buffers_indexed") != NULL;
    blending_functions_t* blend = &context->blending;
    blend->available = true;
#define SET_FUNC(type) \
    blend->enablei = es3_functions.glEnablei ## type; \
    blend->disablei = es3_functions.glDisablei ## type; \
    blend->isenabledi = es3_functions.glIsEnabledi ## type; \
    blend->blendequationi = es3_functions.glBlendEquationi ## type; \
    blend->blendequationseparatei = es3_functions.glBlendEquationSeparatei ## type; \
    blend->blendfunci = es3_functions.glBlendFunci ## type; \
    blend->blendfuncseparatei = es3_functions.glBlendFuncSeparatei ## type; \
    blend->colormaski = es3_functions.glColorMaski ## type;

    if(context->es32 && es3_functions.glBlendEquationi){
        SET_FUNC()
    }
    else if(drawbuffersi_oes && es3_functions.glBlendEquationiOES){
        SET_FUNC(OES)
    }
    else if(drawbuffersi_ext && es3_functions.glBlendEquationiEXT){
        SET_FUNC(EXT)
    }
    else {
        blend->available = false;
    }
#undef SET_FUNC
    build_extension_string(context);

    return;
    fail:
    printf("EmesisRenderer: Failed to detect OpenGL ES version\n");
}

void basevertex_init(context_t* context);
void buffer_copier_init(context_t* context);
static void init_incontext(context_t* tw_context) {
    es3_functions.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &tw_context->maxTextureSize);
    es3_functions.glGetIntegerv(GL_MAX_DRAW_BUFFERS, &tw_context->max_drawbuffers);
    es3_functions.glGetIntegerv(GL_NUM_EXTENSIONS, &tw_context->nextensions_es);
    if(tw_context->max_drawbuffers > MAX_DRAWBUFFERS) {
        tw_context->max_drawbuffers = MAX_DRAWBUFFERS;
    }

    find_esversion(tw_context);

    basevertex_init(tw_context);
    buffer_copier_init(tw_context);
    es3_functions.glGenBuffers(1, &tw_context->multidraw_element_buffer);
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) {
    EGLContext phys_context = host_eglCreateContext(dpy, config, share_context, attrib_list);
    if(phys_context == EGL_NO_CONTEXT) return phys_context;
    context_t* tw_context = calloc(1, sizeof(context_t));
    if(tw_context == NULL || !init_context(tw_context)) {
        if(tw_context) free(tw_context);
        host_eglDestroyContext(dpy, phys_context);
        return EGL_NO_CONTEXT;
    }
    unordered_map_put(context_map, phys_context, tw_context);
    return phys_context;
}

EGLBoolean eglDestroyContext (EGLDisplay dpy, EGLContext ctx) {
    if(!host_eglDestroyContext(dpy, ctx)) return EGL_FALSE;
    context_t* old_ctx = unordered_map_remove(context_map, ctx);
    if(old_ctx) {
        if (current_context == old_ctx) {
            current_context = NULL;
        }
        free_context(old_ctx);
        free(old_ctx);
    }
    return EGL_TRUE;
}

static context_t* adopt_or_get_context(EGLContext ctx) {
    if(ctx == EGL_NO_CONTEXT || !context_map) return NULL;
    context_t* tw_context = unordered_map_get(context_map, ctx);
    if(tw_context == NULL) {
        tw_context = calloc(1, sizeof(context_t));
        if(tw_context == NULL || !init_context(tw_context)) {
            if(tw_context) free(tw_context);
            return NULL;
        }
        tw_context->phys_context = ctx;
        unordered_map_put(context_map, ctx, tw_context);
    }
    current_context = tw_context;
    if(!tw_context->context_rdy) {
        init_incontext(tw_context);
        tw_context->context_rdy = true;
    }
    return tw_context;
}

void emesis_ensure_current_context() {
    if(current_context) return;
    if(!host_eglGetCurrentContext) return;
    EGLContext hw_ctx = host_eglGetCurrentContext();
    if(hw_ctx == EGL_NO_CONTEXT) return;
    current_context = adopt_or_get_context(hw_ctx);
}

EGLBoolean eglMakeCurrent (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    if(!host_eglMakeCurrent(dpy, draw, read, ctx)) return EGL_FALSE;
    if(ctx == EGL_NO_CONTEXT) {
        current_context = NULL;
        return EGL_TRUE;
    }
    context_t* tw_context = adopt_or_get_context(ctx);
    if(tw_context == NULL) {
        printf("EmesisEGL: Failed to initialize context %p\n", ctx);
        return EGL_FALSE;
    }
    current_context = tw_context;
    return EGL_TRUE;
}
