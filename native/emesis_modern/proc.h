/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#ifndef EMESIS_MODERN_PROC_H
#define EMESIS_MODERN_PROC_H

#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
typedef double GLdouble;
#if !defined(__cplusplus) && !defined(thread_local)
#define thread_local _Thread_local
#endif
#ifndef GL_EXT_clear_texture
typedef void (GL_APIENTRYP PFNGLCLEARTEXIMAGEEXTPROC) (GLuint texture, GLint level, GLenum format, GLenum type, const void *data);
typedef void (GL_APIENTRYP PFNGLCLEARTEXSUBIMAGEEXTPROC) (GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *data);
#endif
#ifndef GL_EXT_multi_draw_arrays
typedef void (GL_APIENTRYP PFNGLMULTIDRAWARRAYSEXTPROC) (GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount);
typedef void (GL_APIENTRYP PFNGLMULTIDRAWELEMENTSEXTPROC) (GLenum mode, const GLsizei *count, GLenum type, const void *const*indices, GLsizei primcount);
#endif
typedef void (GL_APIENTRYP PFNGLMULTIDRAWELEMENTSBASEVERTEXEXTPROC) (GLenum mode, const GLsizei *count, GLenum type, const void *const*indices, GLsizei drawcount, const GLint *basevertex);
typedef void (GL_APIENTRYP PFNGLMULTIDRAWELEMENTSBASEVERTEXOESPROC) (GLenum mode, const GLsizei *count, GLenum type, const void *const*indices, GLsizei drawcount, const GLint *basevertex);
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTUREGENERICPROC) (GLenum target, GLenum attachment, GLuint texture, GLint level);

typedef void (*eglMustCastToProperFunctionPointerType)(void);

typedef struct {
#define GLESFUNC(name, type) type name;
#include "es3_functions.h"
#include "es3_extended.h"
#undef GLESFUNC
} es3_functions_t;

extern eglMustCastToProperFunctionPointerType (*host_eglGetProcAddress)(const char *procname);
extern es3_functions_t es3_functions;
void* resolve_egl_symbol(const char *procname);

#endif //EMESIS_MODERN_PROC_H
