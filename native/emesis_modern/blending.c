//
// Created by whbex on 22.11.2025.
//

#include <GL/gl.h>
#include <GL/glext.h>
#include "egl.h"
#include "proc.h"

void glEnablei(GLenum target, GLuint index) {
    emesis_ensure_current_context();
    if (!current_context) return;
    if (current_context->blending.available && current_context->blending.enablei) {
        current_context->blending.enablei(target, index);
    } else if (index == 0) {
        es3_functions.glEnable(target);
    }
}

void glDisablei(GLenum target, GLuint index) {
    emesis_ensure_current_context();
    if (!current_context) return;
    if (current_context->blending.available && current_context->blending.disablei) {
        current_context->blending.disablei(target, index);
    } else if (index == 0) {
        es3_functions.glDisable(target);
    }
}

GLboolean glIsEnabledi(GLenum target, GLuint index) {
    emesis_ensure_current_context();
    if (!current_context) return GL_FALSE;
    if (current_context->blending.available && current_context->blending.isenabledi) {
        return current_context->blending.isenabledi(target, index);
    }
    if (index == 0) {
        return es3_functions.glIsEnabled(target);
    }
    return GL_FALSE;
}

void glBlendEquationi(GLuint buf, GLenum mode) {
    emesis_ensure_current_context();
    if (!current_context) return;
    if (current_context->blending.available && current_context->blending.blendequationi) {
        current_context->blending.blendequationi(buf, mode);
    } else if (buf == 0) {
        es3_functions.glBlendEquation(mode);
    }
}

void glBlendEquationSeparatei(GLuint buf, GLenum modeRGB, GLenum modeAlpha) {
    emesis_ensure_current_context();
    if (!current_context) return;
    if (current_context->blending.available && current_context->blending.blendequationseparatei) {
        current_context->blending.blendequationseparatei(buf, modeRGB, modeAlpha);
    } else if (buf == 0) {
        es3_functions.glBlendEquationSeparate(modeRGB, modeAlpha);
    }
}

void glBlendFunci(GLuint buf, GLenum src, GLenum dst) {
    emesis_ensure_current_context();
    if (!current_context) return;
    if (current_context->blending.available && current_context->blending.blendfunci) {
        current_context->blending.blendfunci(buf, src, dst);
    } else if (buf == 0) {
        es3_functions.glBlendFunc(src, dst);
    }
}

void glBlendFuncSeparatei(GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    emesis_ensure_current_context();
    if (!current_context) return;
    if (current_context->blending.available && current_context->blending.blendfuncseparatei) {
        current_context->blending.blendfuncseparatei(buf, srcRGB, dstRGB, srcAlpha, dstAlpha);
    } else if (buf == 0) {
        es3_functions.glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
    }
}

void glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    emesis_ensure_current_context();
    if (!current_context) return;
    if (current_context->blending.available && current_context->blending.colormaski) {
        current_context->blending.colormaski(index, r, g, b, a);
    } else if (index == 0) {
        es3_functions.glColorMask(r, g, b, a);
    }
}

void glBlendEquationiARB(GLuint buf, GLenum mode) {
    glBlendEquationi(buf, mode);
}

void glBlendEquationSeparateiARB(GLuint buf, GLenum modeRGB, GLenum modeAlpha) {
    glBlendEquationSeparatei(buf, modeRGB, modeAlpha);
}

void glBlendFunciARB(GLuint buf, GLenum src, GLenum dst) {
    glBlendFunci(buf, src, dst);
}

void glBlendFuncSeparateiARB(GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    glBlendFuncSeparatei(buf, srcRGB, dstRGB, srcAlpha, dstAlpha);
}