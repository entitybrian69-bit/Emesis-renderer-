/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#ifndef GL4ES_WRAPPER_BASEVERTEX_H
#define GL4ES_WRAPPER_BASEVERTEX_H

#include "egl.h"

GLint type_bytes(GLenum type);
bool emesis_dispatch_mdi_commands(context_t* ctx, GLenum mode, GLenum type,
                                  const basevertex_indirect_cmd_t* cmds, GLsizei valid_draws);

#endif //GL4ES_WRAPPER_BASEVERTEX_H
