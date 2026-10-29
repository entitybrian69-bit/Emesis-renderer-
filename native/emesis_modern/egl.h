/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#ifndef EMESIS_MODERN_EGL_H
#define EMESIS_MODERN_EGL_H

#include <stdbool.h>
#include <stddef.h>
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

#define EMESIS_FAST_VAO_MAX 4096
#define EMESIS_FAST_TEX_MAX 16384
#define EMESIS_FAST_BUF_MAX 16384

#define EMESIS_TEX_FLAG_SWIZZLE 0x01u
#define EMESIS_TEX_FLAG_DEPTH   0x02u

#define EMESIS_BUF_FLAG_COHERENT_MAPPED 0x01u

typedef struct {
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLint baseVertex;
    GLuint reservedMustBeZero;
} basevertex_indirect_cmd_t;

typedef struct {
    uint8_t* data;
    size_t size;
    size_t cap;
    bool valid;
} es30_ebo_mirror_t;

typedef struct {
    bool ready;
    GLuint indirectRenderBuffer;
    void* indirectMappedPtr;
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
    GLint  color_levels[MAX_FBTARGETS];
    GLint  color_layers[MAX_FBTARGETS];
    GLuint phys_targets[MAX_FBTARGETS];
    GLuint phys_objects[MAX_FBTARGETS];
    GLint  phys_levels[MAX_FBTARGETS];
    GLint  phys_layers[MAX_FBTARGETS];
    GLenum virt_drawbuffers[MAX_DRAWBUFFERS];
    GLenum phys_drawbuffers[MAX_DRAWBUFFERS];
    GLenum sent_es_drawbuffers[MAX_DRAWBUFFERS];
    GLsizei nbuffers;
    GLsizei sent_es_nbuffers;
    bool attachments_dirty;
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
    bool es31, es32, buffer_storage, buffer_texture_ext, buffer_texture_oes;
    bool multidraw_indirect, multidraw_ext, timer_query, base_instance, clear_texture;
    bool es30_needs_ebo_mirror;
    int8_t mdi_probe_state;
    int8_t mdbv_probe_state;
    int8_t mda_probe_state;
    GLint shader_version;
    char gpu_vendor[64];
    char gpu_renderer[96];
    char gpu_version[96];
    basevertex_renderer_t basevertex;
    PFNGLDRAWELEMENTSBASEVERTEXPROC drawelementsbasevertex;
    PFNGLMULTIDRAWELEMENTSBASEVERTEXEXTPROC multidrawelementsbasevertex;
    PFNGLCOPYIMAGESUBDATAPROC copyimagesubdata;
    PFNGLFRAMEBUFFERTEXTUREGENERICPROC framebuffertexture;
    blending_functions_t blending;
    GLuint multidraw_element_buffer;
    GLuint clear_fbo;
    framebuffer_copier_t framebuffer_copier;
    unordered_map* shader_map;
    unordered_map* program_map;
    unordered_map* framebuffer_map;
    unordered_map* texture_swztrack_map;
    unordered_map* vao_ebo_map;
    unordered_map* buf_coherent_map;
    unordered_map* es30_ebo_mirror_map;
    GLuint current_vao;
    bool current_vao_valid;
    GLuint default_vao_ebo;
    GLuint vao_ebo_fast[EMESIS_FAST_VAO_MAX];
    uint8_t tex_flags_fast[EMESIS_FAST_TEX_MAX];
    uint8_t tex_target_fast[EMESIS_FAST_TEX_MAX];
    GLenum tex_intfmt_fast[EMESIS_FAST_TEX_MAX];
    uint8_t buf_flags_fast[EMESIS_FAST_BUF_MAX];
    GLuint active_tex_unit;
    bool active_tex_unit_valid;
    GLuint bound_textures[MAX_TMUS][MAX_TEXTARGETS];
    uint16_t bound_textures_valid[MAX_TMUS];
    GLuint bound_samplers[MAX_TMUS];
    uint32_t bound_samplers_valid_mask;
    ubo_slot_binding_t ubo_bindings[MAX_UBO_SLOTS];
    int proxy_width, proxy_height, proxy_intformat, maxTextureSize;
    GLint max_drawbuffers;
    GLuint bound_buffers[MAX_BOUND_BUFFERS];
    uint16_t bound_buffers_valid_mask;
    GLuint program;
    GLuint draw_framebuffer;
    GLuint read_framebuffer;
    bool draw_framebuffer_valid;
    bool read_framebuffer_valid;
    char* extensions_string;
    size_t nextras;
    int nextensions_es;
    char** extra_extensions_array;
} context_t;

extern thread_local context_t *current_context;
extern void init_egl(void);
extern context_t* emesis_ensure_current_context_slow(void);

static inline __attribute__((always_inline)) context_t* emesis_get_context(void) {
    context_t* ctx = current_context;
    if (__builtin_expect(ctx != NULL, 1)) return ctx;
    return emesis_ensure_current_context_slow();
}

static inline __attribute__((always_inline)) void emesis_ensure_current_context(void) {
    if (__builtin_expect(current_context == NULL, 0)) {
        (void)emesis_ensure_current_context_slow();
    }
}

extern GLenum get_textarget_query_param(GLenum target);
extern int emesis_tex_target_index(GLenum target);
extern GLenum emesis_tex_index_to_target(int idx);
extern GLuint emesis_get_bound_texture(GLenum target);
extern GLuint emesis_get_current_ebo(void);
extern void emesis_es30_on_buffer_data(context_t* ctx, GLenum target, GLsizeiptr size, const void* data);
extern void emesis_es30_on_buffer_sub_data(context_t* ctx, GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
extern void emesis_es30_on_copy_buffer_sub_data(context_t* ctx, GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size);
extern void emesis_es30_invalidate_buffer(context_t* ctx, GLuint buf);
extern void emesis_es30_delete_buffer(context_t* ctx, GLuint buf);
extern const uint8_t* emesis_es30_get_ebo_cpu_ptr(context_t* ctx, GLuint ebo, size_t min_needed_end);

#endif //EMESIS_MODERN_EGL_H
