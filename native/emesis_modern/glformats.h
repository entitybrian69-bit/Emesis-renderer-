/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#ifndef EMESIS_MODERN_GLFORMATS_H
#define EMESIS_MODERN_GLFORMATS_H

#include <GLES3/gl3.h>

extern void pick_internalformat(GLint *internalformat, GLenum* type, GLenum* format, GLvoid const** data);
extern GLenum remap_tex_storage_internalformat(GLenum internalformat);

#endif //EMESIS_MODERN_GLFORMATS_H
