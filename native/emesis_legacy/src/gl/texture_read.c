#include "texture.h"

#include "../glx/hardext.h"
#include "../glx/streaming.h"
#include "array.h"
#include "blit.h"
#include "decompress.h"
#include "debug.h"
#include "enum_info.h"
#include "fpe.h"
#include "framebuffers.h"
#include "GL/gl.h"
#include "init.h"
#include "loader.h"
#include "matrix.h"
#include "pixel.h"
#include "raster.h"

// #define DEBUG
#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

static int inline nlevel(int size, int level) {
    if (size) {
        size >>= level;
        if (!size) size = 1;
    }
    return size;
}

static int is_depth_format(GLenum format) {
    switch (format) {
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32:
    case GL_DEPTH_COMPONENT32F:
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
        return 1;
    default:
        return 0;
    }
}

static int is_float_color_format(GLenum internalFormat, GLenum type) {
    if (type == GL_FLOAT || type == GL_HALF_FLOAT || type == GL_HALF_FLOAT_OES ||
        type == GL_UNSIGNED_INT_10F_11F_11F_REV || type == GL_UNSIGNED_INT_5_9_9_9_REV) {
        return 1;
    }
    switch (internalFormat) {
    case GL_R16F:
    case GL_RG16F:
    case GL_RGB16F:
    case GL_RGBA16F:
    case GL_R32F:
    case GL_RG32F:
    case GL_RGB32F:
    case GL_RGBA32F:
    case GL_R11F_G11F_B10F:
    case GL_RGB9_E5:
        return 1;
    default:
        return 0;
    }
}

#define COPY_BLIT_FBO_CACHE_SLOTS 16
typedef struct {
    GLuint tex;
    GLenum target;
    GLint level;
    GLenum attach;
    GLuint fbo;
    GLuint age;
    int complete;
    uint8_t has_draw_cfg;
    uint8_t has_read_cfg;
} copy_blit_fbo_slot_t;

static __thread copy_blit_fbo_slot_t s_copyBlitCache[COPY_BLIT_FBO_CACHE_SLOTS];
static __thread void* s_copyBlitOwner = NULL;
static __thread GLuint s_copyBlitClock = 0;

void gl4es_invalidate_copy_fbo_cache(GLuint tex_glname) {
    if (!tex_glname || !glstate || s_copyBlitOwner != (void*)glstate) return;
    LOAD_GLES2_OR_OES(glDeleteFramebuffers);
    for (int i = 0; i < COPY_BLIT_FBO_CACHE_SLOTS; ++i) {
        if (s_copyBlitCache[i].tex == tex_glname && s_copyBlitCache[i].fbo) {
            if (gles_glDeleteFramebuffers) {
                gles_glDeleteFramebuffers(1, &s_copyBlitCache[i].fbo);
            }
            memset(&s_copyBlitCache[i], 0, sizeof(s_copyBlitCache[i]));
        }
    }
}

static GLuint bind_cached_copy_blit_fbo(GLenum fb_target, GLuint tex_glname, GLenum tex_target, GLint level,
                                        GLenum attach, int* out_complete) {
    LOAD_GLES2(glGenFramebuffers);
    LOAD_GLES2(glBindFramebuffer);
    LOAD_GLES2(glFramebufferTexture2D);
    LOAD_GLES2(glCheckFramebufferStatus);
    if (!tex_glname || !gles_glGenFramebuffers || !gles_glBindFramebuffer || !gles_glFramebufferTexture2D) {
        if (out_complete) *out_complete = 0;
        return 0;
    }
    if (s_copyBlitOwner != (void*)glstate) {
        memset(s_copyBlitCache, 0, sizeof(s_copyBlitCache));
        s_copyBlitOwner = (void*)glstate;
        s_copyBlitClock = 0;
    }
    int victim = 0;
    GLuint oldest_age = ~0u;
    for (int i = 0; i < COPY_BLIT_FBO_CACHE_SLOTS; ++i) {
        if (s_copyBlitCache[i].tex == tex_glname && s_copyBlitCache[i].target == tex_target &&
            s_copyBlitCache[i].level == level && s_copyBlitCache[i].attach == attach && s_copyBlitCache[i].fbo) {
            s_copyBlitCache[i].age = ++s_copyBlitClock;
            gles_glBindFramebuffer(fb_target, s_copyBlitCache[i].fbo);
            if (glstate) glstate->fbo.gles_fb_synced = 0;
            if (hardext.esversion >= 3) {
                GLenum buf_mode = (attach == GL_COLOR_ATTACHMENT0) ? GL_COLOR_ATTACHMENT0 : GL_NONE;
                if (fb_target != GL_READ_FRAMEBUFFER && !s_copyBlitCache[i].has_draw_cfg) {
                    LOAD_GLES2(glDrawBuffers);
                    if (gles_glDrawBuffers) gles_glDrawBuffers(1, &buf_mode);
                    s_copyBlitCache[i].has_draw_cfg = 1;
                }
                if (fb_target != GL_DRAW_FRAMEBUFFER && !s_copyBlitCache[i].has_read_cfg) {
                    LOAD_GLES2(glReadBuffer);
                    if (gles_glReadBuffer) gles_glReadBuffer(buf_mode);
                    s_copyBlitCache[i].has_read_cfg = 1;
                }
            }
            if (out_complete) *out_complete = s_copyBlitCache[i].complete;
            return s_copyBlitCache[i].fbo;
        }
        if (!s_copyBlitCache[i].fbo) {
            victim = i;
            oldest_age = 0;
        } else if (oldest_age != 0 && s_copyBlitCache[i].age < oldest_age) {
            oldest_age = s_copyBlitCache[i].age;
            victim = i;
        }
    }
    copy_blit_fbo_slot_t* slot = &s_copyBlitCache[victim];
    if (!slot->fbo) {
        gles_glGenFramebuffers(1, &slot->fbo);
        if (!slot->fbo) {
            if (out_complete) *out_complete = 0;
            return 0;
        }
        gles_glBindFramebuffer(fb_target, slot->fbo);
    } else {
        gles_glBindFramebuffer(fb_target, slot->fbo);
        if (slot->attach && slot->attach != attach) {
            gles_glFramebufferTexture2D(fb_target, slot->attach, slot->target ? slot->target : GL_TEXTURE_2D, 0, 0);
        }
    }
    if (glstate) glstate->fbo.gles_fb_synced = 0;
    slot->has_draw_cfg = 0;
    slot->has_read_cfg = 0;
    gles_glFramebufferTexture2D(fb_target, attach, tex_target, tex_glname, level);
    if (hardext.esversion >= 3) {
        GLenum buf_mode = (attach == GL_COLOR_ATTACHMENT0) ? GL_COLOR_ATTACHMENT0 : GL_NONE;
        if (fb_target != GL_READ_FRAMEBUFFER) {
            LOAD_GLES2(glDrawBuffers);
            if (gles_glDrawBuffers) gles_glDrawBuffers(1, &buf_mode);
            slot->has_draw_cfg = 1;
        }
        if (fb_target != GL_DRAW_FRAMEBUFFER) {
            LOAD_GLES2(glReadBuffer);
            if (gles_glReadBuffer) gles_glReadBuffer(buf_mode);
            slot->has_read_cfg = 1;
        }
    }
    int complete = (!gles_glCheckFramebufferStatus ||
                    gles_glCheckFramebufferStatus(fb_target) == GL_FRAMEBUFFER_COMPLETE)
                       ? 1
                       : 0;
    slot->tex = tex_glname;
    slot->target = tex_target;
    slot->level = level;
    slot->attach = attach;
    slot->age = ++s_copyBlitClock;
    slot->complete = complete;
    if (out_complete) *out_complete = complete;
    return slot->fbo;
}

static GLenum get_binding_for_target(GLenum target) {
    switch (target) {
    case GL_TEXTURE_2D:
        return GL_TEXTURE_BINDING_2D;
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        return GL_TEXTURE_BINDING_CUBE_MAP;
    default:
        return 0;
    }
}

void APIENTRY_GL4ES gl4es_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y,
                                           GLsizei width, GLsizei height, GLint border) {
    DBG(SHUT_LOGD("glCopyTexImage2D(%s, %i, %s, %i, %i, %i, %i, %i), glstate->fbo.current_fb=%p\n", PrintEnum(target),
                  level, PrintEnum(internalformat), x, y, width, height, border, glstate->fbo.current_fb);)
    // PUSH_IF_COMPILING(glCopyTexImage2D);
    FLUSH_BEGINEND;
    const GLuint itarget = what_target(target);

    // actually bound if targeting shared TEX2D
    realize_bound(glstate->texture.active, target);

    if (globals4es.skiptexcopies) {
        DBG(SHUT_LOGD("glCopyTexImage2D skipped.\n"));
        return;
    }

    errorGL();

    // "Unmap" if buffer mapped...
    glbuffer_t* pack = glstate->vao->pack;
    glbuffer_t* unpack = glstate->vao->unpack;
    glstate->vao->pack = NULL;
    glstate->vao->unpack = NULL;

    readfboBegin(); // multiple readfboBegin() can be chained...
    gltexture_t* bound = glstate->texture.bound[glstate->texture.active][itarget];

    if (glstate->fbo.current_fb->read_type == 0) {
        LOAD_GLES(glGetIntegerv);
        gles_glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES, (GLint*)&glstate->fbo.current_fb->read_format);
        gles_glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE_OES, (GLint*)&glstate->fbo.current_fb->read_type);
    }

    int copytex = (!is_depth_format(internalformat)) || (bound->format == glstate->fbo.current_fb->read_format &&
                                                         bound->type == glstate->fbo.current_fb->read_type);

    if (copytex) {
        GLenum fmt;
        switch (internalformat) {
        case GL_ALPHA:
        case GL_ALPHA8:
            fmt = GL_ALPHA;
            break;
        case GL_LUMINANCE:
        case GL_LUMINANCE8:
            fmt = GL_LUMINANCE;
            break;
        case GL_LUMINANCE_ALPHA:
        case GL_LUMINANCE8_ALPHA8:
            fmt = GL_LUMINANCE_ALPHA;
            break;
        case GL_RGB:
        case 3:
            fmt = GL_RGB;
            break;
        default:
            fmt = GL_RGBA;
        }
        LOAD_GLES(glCopyTexImage2D);
        gles_glCopyTexImage2D(target, level, fmt, x, y, width, height, border);
    } else {
        DBG(SHUT_LOGD(" -No use of gles_glCopyTexImage2D"))

        LOAD_GLES(glTexImage2D)
        LOAD_GLES(glBindFramebuffer)
        LOAD_GLES(glBlitFramebuffer)
        GLenum format = GL_DEPTH_COMPONENT;
        GLenum type = GL_UNSIGNED_INT;
        internal2format_type(&internalformat, &format, &type);
        gles_glTexImage2D(target, level, (GLint)internalformat, width, height, border, format, type, NULL);
        GLuint prevDrawFBO = (glstate->fbo.fbo_draw && glstate->fbo.fbo_draw->id) ? glstate->fbo.fbo_draw->id
                                                                                  : glstate->fbo.mainfbo_fbo;
        GLuint dst_tex = bound ? bound->glname : 0;
        int fb_ok = 0;
        if (dst_tex) {
            bind_cached_copy_blit_fbo(GL_DRAW_FRAMEBUFFER, dst_tex, target, level, GL_DEPTH_ATTACHMENT, &fb_ok);
        }
        if (fb_ok && gles_glBlitFramebuffer) {
            LOAD_GLES(glDisable);
            LOAD_GLES(glEnable);
            int had_scissor = glstate->enable.scissor;
            if (had_scissor && gles_glDisable) gles_glDisable(GL_SCISSOR_TEST);
            gles_glBlitFramebuffer(x, y, x + width, y + height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            if (had_scissor && gles_glEnable) gles_glEnable(GL_SCISSOR_TEST);
        }
        gles_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    }

    readfboEnd();
    // "Remap" if buffer mapped...
    glstate->vao->pack = pack;
    glstate->vao->unpack = unpack;
}

void APIENTRY_GL4ES gl4es_glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x,
                                              GLint y, GLsizei width, GLsizei height) {
    const GLuint itarget = what_target(target);
    // WARNING: It seems glColorMask has an impact on what channel are actually copied by this. The crude glReadPixel /
    // glTexSubImage cannot emulate that, and proper emulation will take need 2 read pixels.
    //  And using the real glCopyTexSubImage2D needs that the FrameBuffer were data are read is compatible with the
    //  Texture it's copied to...
    DBG(SHUT_LOGD("glCopyTexSubImage2D(%s, %i, %i, %i, %i, %i, %i, %i), bounded texture=%u format/type=%s, %s\n",
                  PrintEnum(target), level, xoffset, yoffset, x, y, width, height,
                  (glstate->texture.bound[glstate->texture.active][itarget])
                      ? glstate->texture.bound[glstate->texture.active][itarget]->texture
                      : 0,
                  PrintEnum((glstate->texture.bound[glstate->texture.active][itarget])
                                ? glstate->texture.bound[glstate->texture.active][itarget]->format
                                : 0),
                  PrintEnum((glstate->texture.bound[glstate->texture.active][itarget])
                                ? glstate->texture.bound[glstate->texture.active][itarget]->type
                                : 0));)
    // PUSH_IF_COMPILING(glCopyTexSubImage2D);
    FLUSH_BEGINEND;

    if (globals4es.skiptexcopies) {
        DBG(SHUT_LOGD("glCopyTexSubImage2D skipped.\n"));
        return;
    }

    LOAD_GLES(glCopyTexSubImage2D);
    errorGL();
    realize_bound(glstate->texture.active, target);

    // "Unmap" if buffer mapped...
    glbuffer_t* pack = glstate->vao->pack;
    glbuffer_t* unpack = glstate->vao->unpack;
    glstate->vao->pack = NULL;
    glstate->vao->unpack = NULL;

    readfboBegin(); // multiple readfboBegin() can be chained...

    gltexture_t* bound = glstate->texture.bound[glstate->texture.active][itarget];
#ifdef TEXSTREAM
    if (bound->streamed) {
        void* buff = GetStreamingBuffer(bound->streamingID);
        if ((bound->width == width) && (bound->height == height) && (xoffset == 0) && (yoffset == 0)) {
            gl4es_glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, buff);
        } else {
            void* tmp = malloc(width * height * 2);
            gl4es_glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, tmp);
            for (int y = 0; y < height; y++) {
                memcpy(buff + ((yoffset + y) * bound->width + xoffset) * 2, tmp + y * width * 2, width * 2);
            }
            free(tmp);
        }
    } else
#endif
    {
        GLint internalFormat = (GLint)(bound->internalformat ? bound->internalformat : bound->format);
        if (!bound->internalformat && hardext.esversion >= 3) {
            LOAD_GLES2(glGetTexLevelParameteriv)
            if (gles_glGetTexLevelParameteriv) {
                GLint queried = 0;
                gles_glGetTexLevelParameteriv(target, level, GL_TEXTURE_INTERNAL_FORMAT, &queried);
                if (queried != 0) {
                    internalFormat = queried;
                    bound->internalformat = (GLenum)queried;
                }
            }
        }
        int is_depth = is_depth_format((GLenum)internalFormat) || is_depth_format(bound->format);
        int is_flt_color = !is_depth && is_float_color_format((GLenum)internalFormat, bound->type);
        int copytex = (!is_depth && !is_flt_color);
        if (!copytex && !is_depth && !is_flt_color) {
            if (glstate->fbo.current_fb->read_type == 0) {
                LOAD_GLES(glGetIntegerv);
                gles_glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES,
                                   (GLint*)&glstate->fbo.current_fb->read_format);
                gles_glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE_OES,
                                   (GLint*)&glstate->fbo.current_fb->read_type);
            }
            copytex = (bound->format == glstate->fbo.current_fb->read_format &&
                       bound->type == glstate->fbo.current_fb->read_type);
        }
        if (copytex || (!is_depth && !is_flt_color &&
                        (!glstate->colormask[0] || !glstate->colormask[1] || !glstate->colormask[2] ||
                         !glstate->colormask[3]))) {
            gles_glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
            if (((((bound->max_level == level) && (level || bound->mipmap_need)) &&
                  (GL4ES_AUTOMIPMAP_PLACEHOLDER != 3) && (bound->mipmap_need != 0))) &&
                !(bound->max_level == bound->base_level && bound->base_level == 0)) {
                LOAD_GLES2_OR_OES(glGenerateMipmap);
                if (gles_glGenerateMipmap) {
                    gles_glGenerateMipmap(to_target(itarget));
                }
            }
        } else {
            DBG(SHUT_LOGD(" -BlitFBO path for depth/float glCopyTexSubImage2D");)
            LOAD_GLES2(glBindFramebuffer)
            LOAD_GLES2(glBlitFramebuffer)

            GLuint dstTex = bound->glname;
            if (!dstTex) {
                LOAD_GLES2(glGetIntegerv)
                GLint currentTex = 0;
                gles_glGetIntegerv(get_binding_for_target(target), &currentTex);
                dstTex = (GLuint)currentTex;
            }
            if (gles_glBlitFramebuffer && gles_glBindFramebuffer && dstTex) {
                GLuint restoreDrawFBO = (glstate->fbo.fbo_draw && glstate->fbo.fbo_draw->id)
                                            ? glstate->fbo.fbo_draw->id
                                            : glstate->fbo.mainfbo_fbo;
                GLenum attach = is_depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
                GLbitfield mask = is_depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT;
                if (internalFormat == GL_DEPTH_STENCIL || internalFormat == GL_DEPTH24_STENCIL8 ||
                    internalFormat == GL_DEPTH32F_STENCIL8) {
                    attach = GL_DEPTH_STENCIL_ATTACHMENT;
                }
                int complete = 0;
                bind_cached_copy_blit_fbo(GL_DRAW_FRAMEBUFFER, dstTex, target, level, attach, &complete);
                if (complete) {
                    LOAD_GLES(glDisable);
                    LOAD_GLES(glEnable);
                    int had_scissor = glstate->enable.scissor;
                    if (had_scissor && gles_glDisable) gles_glDisable(GL_SCISSOR_TEST);
                    gles_glBlitFramebuffer(x, y, x + width, y + height, xoffset, yoffset, xoffset + width,
                                           yoffset + height, mask, GL_NEAREST);
                    if (had_scissor && gles_glEnable) gles_glEnable(GL_SCISSOR_TEST);
                } else if (!is_depth) {
                    gles_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restoreDrawFBO);
                    gles_glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
                    readfboEnd();
                    glstate->vao->pack = pack;
                    glstate->vao->unpack = unpack;
                    return;
                }
                gles_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restoreDrawFBO);
            } else {
                gles_glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
            }
        }
    }
    readfboEnd();
    // "Remap" if buffer mapped...
    glstate->vao->pack = pack;
    glstate->vao->unpack = unpack;
}

void APIENTRY_GL4ES gl4es_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                                       GLvoid* data) {
    DBG(SHUT_LOGD("glReadPixels(%i, %i, %i, %i, %s, %s, 0x%p)\n", x, y, width, height, PrintEnum(format),
                  PrintEnum(type), data);)
    FLUSH_BEGINEND;

    if (glstate->list.compiling && glstate->list.active) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }

    LOAD_GLES(glReadPixels);
    errorGL();

    GLvoid* dst = data;
    if (glstate->vao->pack) {
        dst = (char*)dst + (uintptr_t)glstate->vao->pack->data;
    }

    readfboBegin();

    if ((format == GL_RGBA && type == GL_UNSIGNED_BYTE) || (format == glstate->readf && type == glstate->readt) ||
        (format == GL_DEPTH_COMPONENT && (type == GL_FLOAT || type == GL_HALF_FLOAT || type == GL_UNSIGNED_INT))) {
        gles_glReadPixels(x, y, width, height, format, type, dst);
        readfboEnd();
        return;
    }

    int use_bgra = (glstate->readf == GL_BGRA && glstate->readt == GL_UNSIGNED_BYTE) ? 1 : 0;

    GLvoid* pixels = malloc(width * height * 4);
    if (!pixels) {
        LOGE("Memory allocation failed for temporary pixel buffer\n");
        readfboEnd();
        return;
    }

    gles_glReadPixels(x, y, width, height, use_bgra ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    if (!pixel_convert(pixels, &dst, width, height, use_bgra ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, format, type, 0,
                       glstate->texture.pack_align)) {
        LOGE("ReadPixels error: (%s, UNSIGNED_BYTE -> %s, %s )\n", PrintEnum(use_bgra ? GL_BGRA : GL_RGBA),
             PrintEnum(format), PrintEnum(type));
    }

    free(pixels);
    readfboEnd();
}

void APIENTRY_GL4ES gl4es_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid* img) {
    DBG(SHUT_LOGD("glGetTexImage(%s, %i, %s, %s, %p)\n", PrintEnum(target), level, PrintEnum(format), PrintEnum(type),
                  img);)
    FLUSH_BEGINEND;
    const GLuint itarget = what_target(target);

    realize_bound(glstate->texture.active, target);

    gltexture_t* bound = glstate->texture.bound[glstate->texture.active][itarget];
    int width = bound->width;
    int height = bound->height;
    int nwidth = bound->nwidth;
    int nheight = bound->nheight;
    int shrink = bound->shrink;
    if (level != 0) {
        // printf("STUBBED glGetTexImage with level=%i\n", level);
        void* tmp = malloc(width * height * pixel_sizeof(format, type)); // tmp space...
        void* tmp2;
        gl4es_glGetTexImage(map_tex_target(target), 0, format, type, tmp);
        for (int i = 0; i < level; i++) {
            pixel_halfscale(tmp, &tmp2, width, height, format, type);
            free(tmp);
            tmp = tmp2;
            width = nlevel(width, 1);
            height = nlevel(height, 1);
        }
        memcpy(img, tmp, width * height * pixel_sizeof(format, type));
        free(tmp);
        return;
    }

    if (target != GL_TEXTURE_2D) {
        return;
    }

    DBG(SHUT_LOGD("glGetTexImage(%s, %i, %s, %s, 0x%p), texture=0x%x, size=%i,%i\n", PrintEnum(target), level,
                  PrintEnum(format), PrintEnum(type), img, bound->glname, width, height);)

    GLvoid* dst = img;
    if (glstate->vao->pack) dst = (char*)dst + (uintptr_t)glstate->vao->pack->data;
#ifdef TEXSTREAM
    if (globals4es.texstream && bound->streamed) {
        noerrorShim();
        pixel_convert(GetStreamingBuffer(bound->streamingID), &dst, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                      format, type, 0, glstate->texture.unpack_align);
        readfboEnd();
        return;
    }
#endif
    if (globals4es.texcopydata && bound->data) {
        // printf("texcopydata* glGetTexImage(0x%04X, %d, 0x%04x, 0x%04X, %p)\n", target, level, format, type, img);
        noerrorShim();
        if (!pixel_convert(bound->data, &dst, width, height, GL_RGBA, GL_UNSIGNED_BYTE, format, type, 0,
                           glstate->texture.pack_align)) {
            DBG(SHUT_LOGD("LIBGL: Error on pixel_convert while glGetTexImage\n"));
        }
    } else {
        // Setup an FBO the same size of the texture
        GLuint oldBind = bound->glname;
        GLuint old_fbo = glstate->fbo.current_fb->id;
        GLuint fbo;

        // if the texture is not RGBA or RGB or ALPHA, the "just attach texture to the fbo" trick will not work, and a
        // full Blit has to be done
        if ((bound->format == GL_RGBA || bound->format == GL_RGB || (bound->format == GL_BGRA && hardext.bgra8888) ||
             bound->format == GL_ALPHA) &&
            (shrink == 0)) {
            gl4es_glGenFramebuffers(1, &fbo);
            gl4es_glBindFramebuffer(GL_FRAMEBUFFER_OES, fbo);
            gl4es_glFramebufferTexture2D(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, oldBind, 0);
            // Read the pixels!
            gl4es_glReadPixels(0, nheight - height, width, height, format, type,
                               img); // using "full" version with conversion of format/type
            gl4es_glBindFramebuffer(GL_FRAMEBUFFER_OES, old_fbo);
            gl4es_glDeleteFramebuffers(1, &fbo);
            noerrorShim();
        } else {
            gl4es_glGenFramebuffers(1, &fbo);
            gl4es_glBindFramebuffer(GL_FRAMEBUFFER_OES, fbo);
            GLuint temptex;
            gl4es_glGenTextures(1, &temptex);
            gl4es_glBindTexture(GL_TEXTURE_2D, temptex);
            gl4es_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, nwidth << shrink, nheight << shrink, 0, GL_RGBA,
                               GL_UNSIGNED_BYTE, 0);
            gl4es_glFramebufferTexture2D(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, temptex, 0);
            gl4es_glBindTexture(GL_TEXTURE_2D, oldBind);
            // blit the texture
            gl4es_glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            gl4es_glClear(GL_COLOR_BUFFER_BIT);
            gl4es_blitTexture(oldBind, 0.f, 0.f, width, height, nwidth, nheight, 1.0f, 1.0f, nwidth << shrink,
                              nheight << shrink, 0, 0, BLIT_OPAQUE);
            // Read the pixels!
            gl4es_glReadPixels(0, (nheight - height) << shrink, width << shrink, height << shrink, format, type,
                               img); // using "full" version with conversion of format/type
            gl4es_glBindFramebuffer(GL_FRAMEBUFFER_OES, old_fbo);
            gl4es_glDeleteFramebuffers(1, &fbo);
            gl4es_glDeleteTextures(1, &temptex);
            noerrorShim();
        }
    }
}

void APIENTRY_GL4ES gl4es_glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y,
                                           GLsizei width, GLint border) {
    gl4es_glCopyTexImage2D(GL_TEXTURE_1D, level, internalformat, x, y, width, 1, border);
}

void APIENTRY_GL4ES gl4es_glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y,
                                              GLsizei width) {
    gl4es_glCopyTexSubImage2D(GL_TEXTURE_1D, level, xoffset, 0, x, y, width, 1);
}

extern glrenderbuffer_t* find_renderbuffer(GLuint renderbuffer);

void APIENTRY_GL4ES gl4es_glCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY,
                                             GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX,
                                             GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight,
                                             GLsizei srcDepth) {
    FLUSH_BEGINEND;
    noerrorShim();

    GLuint realSrc = srcName;
    GLuint realDst = dstName;
    gltexture_t* srcTex = NULL;
    gltexture_t* dstTex = NULL;

    if (glstate) {
        if (srcTarget == GL_RENDERBUFFER) {
            glrenderbuffer_t* rbo = find_renderbuffer(srcName);
            if (rbo && rbo->renderbuffer) realSrc = rbo->renderbuffer;
        } else if (srcName && glstate->texture.list) {
            khint_t k = kh_get(tex, glstate->texture.list, srcName);
            if (k != kh_end(glstate->texture.list)) {
                srcTex = kh_value(glstate->texture.list, k);
                if (srcTex && srcTex->glname) realSrc = srcTex->glname;
            }
        }
        if (dstTarget == GL_RENDERBUFFER) {
            glrenderbuffer_t* rbo = find_renderbuffer(dstName);
            if (rbo && rbo->renderbuffer) realDst = rbo->renderbuffer;
        } else if (dstName && glstate->texture.list) {
            khint_t k = kh_get(tex, glstate->texture.list, dstName);
            if (k != kh_end(glstate->texture.list)) {
                dstTex = kh_value(glstate->texture.list, k);
                if (dstTex && dstTex->glname) realDst = dstTex->glname;
            }
        }
    }

    /* Fast path for same-format 2D texture-to-texture copies (e.g. Iris depthtex0 -> depthtex1/2):
       cached FBO glBlitFramebuffer pipelines on Mali TBDR tile buffers without draining fragment jobs */
    if (srcTarget == GL_TEXTURE_2D && dstTarget == GL_TEXTURE_2D && srcDepth == 1 && srcZ == 0 && dstZ == 0) {
        LOAD_GLES2(glBindFramebuffer);
        LOAD_GLES2(glBlitFramebuffer);
        if (gles_glBlitFramebuffer && gles_glBindFramebuffer && realSrc && realDst &&
            !(realSrc == realDst && srcLevel == dstLevel)) {
            GLenum srcIF = srcTex ? (srcTex->internalformat ? srcTex->internalformat : srcTex->format) : 0;
            GLenum dstIF = dstTex ? (dstTex->internalformat ? dstTex->internalformat : dstTex->format) : 0;
            if ((!srcIF && !dstIF) || (srcIF && srcIF == dstIF)) {
                GLuint prevRead = (glstate && glstate->fbo.fbo_read && glstate->fbo.fbo_read->id)
                                      ? glstate->fbo.fbo_read->id
                                      : (glstate ? glstate->fbo.mainfbo_fbo : 0);
                GLuint prevDraw = (glstate && glstate->fbo.fbo_draw && glstate->fbo.fbo_draw->id)
                                      ? glstate->fbo.fbo_draw->id
                                      : (glstate ? glstate->fbo.mainfbo_fbo : 0);

                int is_depth = is_depth_format(srcIF) || is_depth_format(dstIF);
                GLenum attach = is_depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
                if (srcIF == GL_DEPTH_STENCIL || srcIF == GL_DEPTH24_STENCIL8 || srcIF == GL_DEPTH32F_STENCIL8 ||
                    dstIF == GL_DEPTH_STENCIL || dstIF == GL_DEPTH24_STENCIL8 || dstIF == GL_DEPTH32F_STENCIL8) {
                    attach = GL_DEPTH_STENCIL_ATTACHMENT;
                }
                GLbitfield mask = is_depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT;

                int r_ok = 0, d_ok = 0;
                bind_cached_copy_blit_fbo(GL_READ_FRAMEBUFFER, realSrc, GL_TEXTURE_2D, srcLevel, attach, &r_ok);
                bind_cached_copy_blit_fbo(GL_DRAW_FRAMEBUFFER, realDst, GL_TEXTURE_2D, dstLevel, attach, &d_ok);
                if (r_ok && d_ok) {
                    LOAD_GLES(glDisable);
                    LOAD_GLES(glEnable);
                    int had_scissor = glstate ? glstate->enable.scissor : 0;
                    if (had_scissor && gles_glDisable) gles_glDisable(GL_SCISSOR_TEST);
                    gles_glBlitFramebuffer(srcX, srcY, srcX + srcWidth, srcY + srcHeight, dstX, dstY, dstX + srcWidth,
                                           dstY + srcHeight, mask, GL_NEAREST);
                    if (had_scissor && gles_glEnable) gles_glEnable(GL_SCISSOR_TEST);
                    if (prevRead == prevDraw) {
                        gles_glBindFramebuffer(GL_FRAMEBUFFER, prevDraw);
                    } else {
                        gles_glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
                        gles_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
                    }
                    if (glstate) {
                        glstate->fbo.gles_read_fb = prevRead;
                        glstate->fbo.gles_draw_fb = prevDraw;
                        glstate->fbo.gles_fb_synced = 3;
                    }
                    errorGL();
                    return;
                }
                if (prevRead == prevDraw) {
                    gles_glBindFramebuffer(GL_FRAMEBUFFER, prevDraw);
                } else {
                    gles_glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
                    gles_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
                }
                if (glstate) {
                    glstate->fbo.gles_read_fb = prevRead;
                    glstate->fbo.gles_draw_fb = prevDraw;
                    glstate->fbo.gles_fb_synced = 3;
                }
            }
        }
    }

    typedef void (*glCopyImageSubData_fn)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint,
                                          GLint, GLint, GLsizei, GLsizei, GLsizei);
    static glCopyImageSubData_fn fn_copy_image = NULL;
    static int checked_copy_image = 0;
    if (!checked_copy_image) {
        checked_copy_image = 1;
        if (gles) {
            fn_copy_image = (glCopyImageSubData_fn)proc_address(gles, "glCopyImageSubData");
            if (!fn_copy_image) fn_copy_image = (glCopyImageSubData_fn)proc_address(gles, "glCopyImageSubDataEXT");
            if (!fn_copy_image) fn_copy_image = (glCopyImageSubData_fn)proc_address(gles, "glCopyImageSubDataOES");
        }
    }

    if (fn_copy_image) {
        fn_copy_image(realSrc, srcTarget, srcLevel, srcX, srcY, srcZ, realDst, dstTarget, dstLevel, dstX, dstY, dstZ,
                      srcWidth, srcHeight, srcDepth);
        errorGL();
        return;
    }

    errorShim(GL_INVALID_OPERATION);
}

// Direct wrapper
AliasExport(void, glCopyImageSubData, ,
            (GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName,
             GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight,
             GLsizei srcDepth));
AliasExport(void, glCopyImageSubData, EXT,
            (GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName,
             GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight,
             GLsizei srcDepth));
AliasExport(void, glCopyImageSubData, NV,
            (GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName,
             GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight,
             GLsizei srcDepth));
AliasExport(void, glGetTexImage, , (GLenum target, GLint level, GLenum format, GLenum type, GLvoid* img));
AliasExport(void, glReadPixels, ,
            (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid* data));
AliasExport(void, glCopyTexImage1D, ,
            (GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border));
AliasExport(void, glCopyTexImage2D, ,
            (GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height,
             GLint border));
AliasExport(void, glCopyTexSubImage2D, ,
            (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
             GLsizei height));
AliasExport(void, glCopyTexSubImage1D, , (GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width));
