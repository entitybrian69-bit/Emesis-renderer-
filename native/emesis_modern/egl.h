/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#ifndef EMESIS_MODERN_EGL_H
#define EMESIS_MODERN_EGL_H

#include <stdbool.h>
#include <stdint.h>
#include <EGL/egl.h>
#include "proc.h"
#include "unordered_map/unordered_map.h"

#define MAX_BOUND_BUFFERS 12
#define MAX_DRAWBUFFERS 8
#define MAX_FBTARGETS 8
#define MAX_TMUS 32
#define MAX_TEXTARGETS 10
#define MAX_UBO_SLOTS 64

typedef struct {
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLint baseVertex;
    GLuint reservedMustBeZero;
} basevertex_indirect_cmd_t;

typedef struct {
    bool ready;
    GLuint indirectRenderBuffer;
    size_t indirectRingSize;
    size_t indirectRingOffset;
    basevertex_indirect_cmd_t* scratchPasses;
    size_t scratchCap;
    GLuint es30IndexBuffer;
    size_t es30IndexRingSize;
    size_t es30IndexRingOffset;
    uint32_t* es30CpuScratch;
    size_t es30CpuCap;
} basevertex_renderer_t;

typedef struct {
    bool available;
    PFNGLENABLEIPROC enablei;
    PFNGLDISABLEIPROC disablei;
    PFNGLISENABLEDIPROC isenabledi;
    PFNGLBLENDEQUATIONIPROC blendequationi;
    PFNGLBLENDEQUATIONSEPARATEIPROC blendequationseparatei;
    PFNGLBLENDFUNCIPROC blendfunci;
    PFNGLBLENDFUNCSEPARATEIPROC blendfuncseparatei;
    PFNGLCOLORMASKIPROC colormaski;
} blending_functions_t;

typedef struct {
    GLuint buffer;
    GLintptr offset;
    GLsizeiptr size;
    bool ranged;
    bool valid;
} ubo_slot_binding_t;

typedef struct {
    GLuint color_targets[MAX_FBTARGETS];
    GLuint color_objects[MAX_FBTARGETS];
    GLuint color_levels[MAX_FBTARGETS];
    GLuint color_layers[MAX_FBTARGETS];
    GLenum virt_drawbuffers[MAX_DRAWBUFFERS];
    GLenum phys_drawbuffers[MAX_DRAWBUFFERS];
    GLsizei nbuffers;
} framebuffer_t;

typedef struct {
    bool ready;
    GLuint temp_texture;
    GLuint tempfb;
    GLuint destfb;
    void* depthData;
    GLsizei depthWidth, depthHeight;
} framebuffer_copier_t;

typedef struct {
    GLenum original_swizzle[4];
    GLboolean goofy_byte_order;
    GLboolean upload_bgra;
} texture_swizzle_track_t;

typedef struct {
    EGLContext phys_context;
    bool context_rdy;
    bool es31, es32, buffer_storage, buffer_texture_ext, buffer_texture_oes, multidraw_indirect, timer_query, base_instance, clear_texture;
    GLint shader_version;
    char gpu_vendor[64];
    char gpu_renderer[96];
    char gpu_version[96];
    basevertex_renderer_t basevertex;
    PFNGLDRAWELEMENTSBASEVERTEXPROC drawelementsbasevertex;
    PFNGLMULTIDRAWELEMENTSBASEVERTEXEXTPROC multidrawelementsbasevertex;
    PFNGLCOPYIMAGESUBDATAPROC copyimagesubdata;
    blending_functions_t blending;
    GLuint multidraw_element_buffer;
    GLuint clear_fbo;
    framebuffer_copier_t framebuffer_copier;
    unordered_map* shader_map;
    unordered_map* program_map;
    unordered_map* framebuffer_map;
    unordered_map* texture_swztrack_map;
    unordered_map* vao_ebo_map;
    GLuint current_vao;
    GLuint default_vao_ebo;
    GLuint active_tex_unit;
    GLuint bound_textures[MAX_TMUS][MAX_TEXTARGETS];
    ubo_slot_binding_t ubo_bindings[MAX_UBO_SLOTS];
    int proxy_width, proxy_height, proxy_intformat, maxTextureSize;
    GLint max_drawbuffers;
    GLuint bound_buffers[MAX_BOUND_BUFFERS];
    GLuint program;
    GLuint draw_framebuffer;
    GLuint read_framebuffer;
    char* extensions_string;
    size_t nextras;
    int nextensions_es;
    char** extra_extensions_array;
} context_t;

extern thread_local context_t *current_context;
extern void init_egl();
extern void emesis_ensure_current_context();
extern GLenum get_textarget_query_param(GLenum target);
extern int emesis_tex_target_index(GLenum target);
extern GLuint emesis_get_bound_texture(GLenum target);
extern GLuint emesis_get_current_ebo(void);

#endif //EMESIS_MODERN_EGL_H
