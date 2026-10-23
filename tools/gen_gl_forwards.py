#!/usr/bin/env python3
"""
Generates typed C forwarder thunks (gl_forwards.inc) strictly for the universal
OpenGL 1.1–3.1 core functions implemented by BOTH libemesis_modern.so and
libemesis_legacy.so.

All backend-specific extensions (e.g., GL 1.x immediate-mode in Legacy, or
GL 3.3+/4.x overrides in Modern) are resolved dynamically via glXGetProcAddress /
glXGetProcAddressARB / eglGetProcAddress so LWJGL's dlsym() fallback never sees
phantom GL 4.x / DSA / compute symbols when running <= 1.16.5 Mods (Sodium, Iris,
OptiFine, Angelica, Oculus).
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
GL_HEADERS = [
    ROOT / "native" / "emesis_modern" / "GL" / "gl.h",
    ROOT / "native" / "emesis_modern" / "GL" / "glext.h",
    ROOT / "native" / "emesis_legacy" / "include" / "GL" / "gl.h",
    ROOT / "native" / "emesis_legacy" / "include" / "GL" / "glext.h",
]

PROTO_RE = re.compile(
    r"^\s*(?:GLAPI|WINGDIAPI|extern)?\s*([A-Za-z0-9_*\s]+?)\s+"
    r"(?:GLAPIENTRY|APIENTRY)\s+(gl[A-Z][A-Za-z0-9_]*)\s*\(([^;]*)\)\s*;"
)

# Exact intersection of OpenGL 1.1-3.1 core entry points implemented by BOTH
# libemesis_legacy.so and libemesis_modern.so / GLES 3.x.
UNIVERSAL_CORE_GL_SYMBOLS = {
    "glActiveTexture", "glAttachShader", "glBeginQuery", "glBindAttribLocation",
    "glBindBuffer", "glBindBufferBase", "glBindBufferRange", "glBindFragDataLocation",
    "glBindFramebuffer", "glBindRenderbuffer", "glBindSampler", "glBindTexture",
    "glBindVertexArray", "glBlendColor", "glBlendEquation", "glBlendEquationSeparate",
    "glBlendFunc", "glBlendFuncSeparate", "glBlitFramebuffer", "glBufferData",
    "glBufferSubData", "glCheckFramebufferStatus", "glClear", "glClearBufferfi",
    "glClearBufferfv", "glClearBufferiv", "glClearBufferuiv", "glClearColor",
    "glClearDepth", "glClearDepthf", "glClearStencil", "glClientWaitSync",
    "glColorMask", "glCompileShader", "glCompressedTexImage2D", "glCompressedTexImage3D",
    "glCompressedTexSubImage2D", "glCompressedTexSubImage3D", "glCopyBufferSubData",
    "glCopyTexImage2D", "glCopyTexSubImage2D", "glCopyTexSubImage3D", "glCreateProgram",
    "glCreateShader", "glCullFace", "glDeleteBuffers", "glDeleteFramebuffers",
    "glDeleteProgram", "glDeleteQueries", "glDeleteRenderbuffers", "glDeleteSamplers",
    "glDeleteShader", "glDeleteSync", "glDeleteTextures", "glDeleteVertexArrays",
    "glDepthFunc", "glDepthMask", "glDepthRange", "glDepthRangef", "glDetachShader",
    "glDisable", "glDisableVertexAttribArray", "glDrawArrays", "glDrawArraysInstanced",
    "glDrawBuffer", "glDrawBuffers", "glDrawElements", "glDrawElementsBaseVertex",
    "glDrawElementsInstanced", "glDrawRangeElements", "glEnable",
    "glEnableVertexAttribArray", "glEndQuery", "glFenceSync", "glFinish", "glFlush",
    "glFlushMappedBufferRange", "glFramebufferRenderbuffer", "glFramebufferTexture2D",
    "glFramebufferTextureLayer", "glFrontFace", "glGenBuffers", "glGenFramebuffers",
    "glGenQueries", "glGenRenderbuffers", "glGenSamplers", "glGenTextures",
    "glGenVertexArrays", "glGenerateMipmap", "glGetActiveAttrib", "glGetActiveUniform",
    "glGetAttachedShaders", "glGetAttribLocation", "glGetBooleanv",
    "glGetBufferParameteriv", "glGetBufferPointerv", "glGetError", "glGetFloatv",
    "glGetFramebufferAttachmentParameteriv", "glGetInteger64i_v", "glGetInteger64v",
    "glGetIntegeri_v", "glGetIntegerv", "glGetProgramBinary",
    "glGetProgramInfoLog", "glGetProgramiv", "glGetQueryObjecti64v",
    "glGetQueryObjectiv", "glGetQueryObjectui64v", "glGetQueryObjectuiv",
    "glGetQueryiv", "glGetRenderbufferParameteriv", "glGetSamplerParameterfv",
    "glGetSamplerParameteriv", "glGetShaderInfoLog", "glGetShaderPrecisionFormat",
    "glGetShaderSource", "glGetShaderiv", "glGetString", "glGetStringi", "glGetSynciv",
    "glGetTexImage", "glGetTexLevelParameterfv", "glGetTexLevelParameteriv",
    "glGetTexParameterfv", "glGetTexParameteriv", "glGetUniformBlockIndex",
    "glGetUniformLocation", "glGetUniformfv", "glGetUniformiv", "glGetUniformuiv",
    "glGetVertexAttribIiv",
    "glGetVertexAttribIuiv", "glGetVertexAttribPointerv", "glGetVertexAttribfv",
    "glGetVertexAttribiv", "glHint", "glIsBuffer", "glIsEnabled", "glIsFramebuffer",
    "glIsProgram", "glIsQuery", "glIsRenderbuffer", "glIsSampler", "glIsShader",
    "glIsSync", "glIsTexture", "glIsVertexArray", "glLineWidth", "glLinkProgram",
    "glMapBuffer", "glMapBufferRange", "glMultiDrawArrays", "glMultiDrawElements",
    "glMultiDrawElementsBaseVertex", "glPixelStorei", "glPolygonOffset",
    "glProgramBinary", "glQueryCounter", "glReadBuffer", "glReadPixels",
    "glReleaseShaderCompiler", "glRenderbufferStorage",
    "glRenderbufferStorageMultisample", "glSampleCoverage", "glSamplerParameterf",
    "glSamplerParameterfv", "glSamplerParameteri", "glSamplerParameteriv", "glScissor",
    "glShaderBinary", "glShaderSource", "glStencilFunc", "glStencilFuncSeparate",
    "glStencilMask", "glStencilMaskSeparate", "glStencilOp", "glStencilOpSeparate",
    "glTexBuffer", "glTexImage2D", "glTexImage3D", "glTexParameterf",
    "glTexParameterfv", "glTexParameteri", "glTexParameteriv", "glTexStorage2D",
    "glTexStorage3D", "glTexSubImage2D", "glTexSubImage3D", "glUniform1f",
    "glUniform1fv", "glUniform1i", "glUniform1iv", "glUniform1ui", "glUniform1uiv",
    "glUniform2f", "glUniform2fv", "glUniform2i", "glUniform2iv", "glUniform2ui", "glUniform2uiv",
    "glUniform3f", "glUniform3fv", "glUniform3i", "glUniform3iv", "glUniform3ui", "glUniform3uiv",
    "glUniform4f", "glUniform4fv", "glUniform4i", "glUniform4iv", "glUniform4ui", "glUniform4uiv",
    "glUniformBlockBinding", "glUniformMatrix2fv", "glUniformMatrix3fv",
    "glUniformMatrix4fv", "glUnmapBuffer", "glUseProgram", "glValidateProgram",
    "glVertexAttrib1d", "glVertexAttrib1dv", "glVertexAttrib1f", "glVertexAttrib1fv",
    "glVertexAttrib1s", "glVertexAttrib1sv", "glVertexAttrib2d", "glVertexAttrib2dv",
    "glVertexAttrib2f", "glVertexAttrib2fv", "glVertexAttrib2s", "glVertexAttrib2sv",
    "glVertexAttrib3d", "glVertexAttrib3dv", "glVertexAttrib3f", "glVertexAttrib3fv",
    "glVertexAttrib3s", "glVertexAttrib3sv", "glVertexAttrib4Nbv",
    "glVertexAttrib4Niv", "glVertexAttrib4Nsv", "glVertexAttrib4Nub",
    "glVertexAttrib4Nubv", "glVertexAttrib4Nuiv", "glVertexAttrib4Nusv",
    "glVertexAttrib4d", "glVertexAttrib4dv", "glVertexAttrib4f", "glVertexAttrib4fv",
    "glVertexAttrib4s", "glVertexAttrib4sv", "glVertexAttribDivisor",
    "glVertexAttribI1i", "glVertexAttribI1iv", "glVertexAttribI1ui",
    "glVertexAttribI1uiv", "glVertexAttribI2i", "glVertexAttribI2iv",
    "glVertexAttribI2ui", "glVertexAttribI2uiv", "glVertexAttribI3i",
    "glVertexAttribI3iv", "glVertexAttribI3ui", "glVertexAttribI3uiv",
    "glVertexAttribI4i", "glVertexAttribI4iv", "glVertexAttribI4ui",
    "glVertexAttribI4uiv", "glVertexAttribIPointer", "glVertexAttribPointer",
    "glViewport", "glWaitSync",
}


def split_params(param_str: str):
    s = param_str.strip()
    if not s or s == "void":
        return []
    parts = [p.strip() for p in s.split(",")]
    result = []
    for idx, p in enumerate(parts):
        arr_m = re.match(r"^(.*?)([A-Za-z_][A-Za-z0-9_]*)\s*\[[^\]]*\]\s*$", p)
        if arr_m:
            base = arr_m.group(1).strip()
            result.append((f"{base} *", f"a{idx}"))
            continue
        m = re.match(r"^(.*?(?:[\s*]))([A-Za-z_][A-Za-z0-9_]*)$", p)
        if m:
            ptype = m.group(1).strip()
            if ptype:
                result.append((ptype, f"a{idx}"))
            else:
                result.append((p, f"a{idx}"))
        else:
            result.append((p, f"a{idx}"))
    return result


def main():
    seen = {}
    for hdr in GL_HEADERS:
        if not hdr.exists():
            continue
        for line in hdr.read_text(encoding="utf-8", errors="ignore").splitlines():
            m = PROTO_RE.match(line)
            if not m:
                continue
            ret_type = " ".join(m.group(1).split())
            name = m.group(2)
            raw_params = m.group(3)
            if name not in UNIVERSAL_CORE_GL_SYMBOLS or name in seen:
                continue
            params = split_params(raw_params)
            seen[name] = (ret_type, params)

    out_lines = [
        "/* Auto-generated by tools/gen_gl_forwards.py — DO NOT EDIT MANUALLY */",
        f"/* Total universal core OpenGL forwarders: {len(seen)} */",
        "",
    ]

    for name, (ret_type, params) in sorted(seen.items()):
        sig_params = ", ".join(f"{pt} {pn}" for pt, pn in params) if params else "void"
        call_args = ", ".join(pn for _, pn in params)
        fn_ptr_params = ", ".join(pt for pt, _ in params) if params else "void"
        is_void = ret_type == "void"

        out_lines.append(f"__attribute__((visibility(\"default\"))) {ret_type} {name}({sig_params}) {{")
        out_lines.append(f"    typedef {ret_type} (*fn_t)({fn_ptr_params});")
        out_lines.append(f"    static fn_t cached = NULL;")
        out_lines.append(f"    fn_t f = __atomic_load_n(&cached, __ATOMIC_ACQUIRE);")
        out_lines.append(f"    if (__builtin_expect(f == NULL, 0)) {{")
        out_lines.append(f"        f = (fn_t)emesis_resolve_gl_symbol(\"{name}\");")
        out_lines.append(f"        __atomic_store_n(&cached, f, __ATOMIC_RELEASE);")
        out_lines.append(f"    }}")
        if is_void:
            out_lines.append(f"    if (f) f({call_args});")
        else:
            out_lines.append(f"    return f ? f({call_args}) : ({ret_type})0;")
        out_lines.append("}")
        out_lines.append("")

    out_path = ROOT / "native" / "emesis_router" / "gl_forwards.inc"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(out_lines), encoding="utf-8")
    print(f"Wrote {len(seen)} universal core GL forwarders to {out_path}")


if __name__ == "__main__":
    main()
