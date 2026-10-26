/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "proc.h"
#include "egl.h"
#include "env.h"
#include "libraryinternal.h"
#include "unordered_map/unordered_map.h"
#define GL_GLEXT_PROTOTYPES
#include "GL/gl.h"
#include "GL/glext.h"

INTERNAL eglMustCastToProperFunctionPointerType (*host_eglGetProcAddress)(const char *procname);
INTERNAL es3_functions_t es3_functions;

static void* s_egl_handle = NULL;
static void* s_gles_handle = NULL;
static unordered_map* s_override_map = NULL;
static bool s_debug_proc = false;

INTERNAL void* resolve_egl_symbol(const char* procname) {
    if (procname == NULL) return NULL;
    void* fn = NULL;
    if (s_egl_handle != NULL) {
        fn = dlsym(s_egl_handle, procname);
    }
    if (fn == NULL && host_eglGetProcAddress != NULL) {
        fn = (void*)host_eglGetProcAddress(procname);
    }
    return fn;
}

extern void init_stub_map(void);
extern void* resolve_stub(const char* procname);

static size_t proc_str_hash(void* key) {
    const unsigned char* s = (const unsigned char*)key;
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint32_t)(*s++);
        h *= 16777619u;
    }
    return (size_t)h;
}

static bool proc_str_eq(void* a, void* b) {
    return strcmp((const char*)a, (const char*)b) == 0;
}

static bool is_self_emesis_lib(const char* path) {
    if (path == NULL || path[0] == '\0') return true;
    const char* base = strrchr(path, '/');
    base = (base != NULL) ? (base + 1) : path;
    return strstr(base, "emesis") != NULL;
}

static void error_sysegl() {
    fprintf(stderr, "EmesisModernInit: Failed to load system EGL: %s\n", dlerror());
    fflush(stderr);
    abort();
}

static void error_init(const char* functionName) {
    fprintf(stderr, "EmesisModernInit: Failed to load function \"%s\"\n", functionName);
    fflush(stderr);
    abort();
}

static void* resolve_core_gles(const char* name) {
    void* fn = NULL;
    if (s_gles_handle != NULL) {
        fn = dlsym(s_gles_handle, name);
    }
    if (fn == NULL && host_eglGetProcAddress != NULL) {
        fn = (void*)host_eglGetProcAddress(name);
    }
    return fn;
}

static void* resolve_ext_gles(const char* name) {
    void* fn = NULL;
    if (host_eglGetProcAddress != NULL) {
        fn = (void*)host_eglGetProcAddress(name);
    }
    if (fn == NULL && s_gles_handle != NULL) {
        fn = dlsym(s_gles_handle, name);
    }
    return fn;
}

static void init_es3_proc() {
#define GLESFUNC(name, type) es3_functions.name = (type)resolve_core_gles(#name); if(es3_functions.name == NULL) error_init(#name);
#include "es3_functions.h"
#undef GLESFUNC
#define GLESFUNC(name, type) es3_functions.name = (type)resolve_ext_gles(#name);
#include "es3_extended.h"
#undef GLESFUNC
}

static void init_override_map(void) {
    if (s_override_map != NULL) return;
    s_override_map = unordered_map_alloc(256, 0.75f, proc_str_hash, proc_str_eq);
    if (s_override_map == NULL) return;
#define GLESOVERRIDE(name) unordered_map_put(s_override_map, (void*)#name, (void*)name);
#include "es3_overrides.h"
#undef GLESOVERRIDE
}

__attribute__((constructor, used)) void proc_init(){
    s_debug_proc = env_istrue("EMESIS_DEBUG");
    const char* systemEglPath = "libEGL.so";
    const char* envEgl = getenv("LIBGL_EGL");
    const char* eglPath = !is_self_emesis_lib(envEgl) ? envEgl : systemEglPath;
    int flags = RTLD_LAZY | RTLD_LOCAL;
    s_egl_handle = dlopen(eglPath, flags);
    if(s_egl_handle == NULL){
        fprintf(stderr, "EmesisModernInit: failed loading custom libEGL (%s), using default\n", eglPath);
        s_egl_handle = dlopen(systemEglPath, flags);
        if(s_egl_handle == NULL)
            error_sysegl();
    }
    host_eglGetProcAddress = dlsym(s_egl_handle, "eglGetProcAddress");
    if(host_eglGetProcAddress == NULL) error_sysegl();

    const char* envGles = getenv("LIBGL_GLES");
    if (!is_self_emesis_lib(envGles)) {
        s_gles_handle = dlopen(envGles, flags);
    }
    if (s_gles_handle == NULL) {
        s_gles_handle = dlopen("libGLESv3.so", flags);
    }
    if (s_gles_handle == NULL) {
        s_gles_handle = dlopen("libGLESv2.so", flags);
    }

    init_egl();
    init_es3_proc();
    init_override_map();
    init_stub_map();
}

// Exported for LWJGL's symbol resolver.
__attribute__((used)) eglMustCastToProperFunctionPointerType glXGetProcAddress(const char *procname) {
    return eglGetProcAddress(procname);
}

__attribute__((used)) eglMustCastToProperFunctionPointerType glXGetProcAddressARB(const char *procname) {
    return eglGetProcAddress(procname);
}

eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname) {
    if (procname == NULL) return NULL;
    // EGL functions that we implement.
    // All of the other platform EGL functions will be redirected into Android's default EGL implementation.
    if(!strncmp(procname, "egl", 3)) {
        if(!strcmp("eglCreateContext", procname)) return (eglMustCastToProperFunctionPointerType) eglCreateContext;
        if(!strcmp("eglDestroyContext", procname)) return (eglMustCastToProperFunctionPointerType) eglDestroyContext;
        if(!strcmp("eglMakeCurrent", procname)) return (eglMustCastToProperFunctionPointerType) eglMakeCurrent;
    }
    // If the function doesn't start with "gl", don't even bother, pass through immediately.
    if(strncmp(procname, "gl", 2) != 0) goto fallback;
    if (s_override_map != NULL) {
        void* ov = unordered_map_get(s_override_map, (void*)procname);
        if (ov != NULL) {
            if (s_debug_proc) printf("EmesisRenderer: Overridden %s\n", procname);
            return (eglMustCastToProperFunctionPointerType)ov;
        }
        goto fallback;
    }
#define GLESOVERRIDE(name)                                        \
    if(!strcmp(procname, #name)) {                                \
        if (s_debug_proc) printf("EmesisRenderer: Overridden %s\n", #name); \
        return (eglMustCastToProperFunctionPointerType) name;     \
    }
#include "es3_overrides.h"
#undef GLESOVERRIDE
fallback:;
    if (!strncmp(procname, "egl", 3)) {
        return (eglMustCastToProperFunctionPointerType)resolve_egl_symbol(procname);
    }
    eglMustCastToProperFunctionPointerType function = host_eglGetProcAddress(procname);
    if(function == NULL && s_gles_handle != NULL) {
        function = (eglMustCastToProperFunctionPointerType)dlsym(s_gles_handle, procname);
    }
    if(function == NULL) {
        function = (eglMustCastToProperFunctionPointerType)resolve_stub(procname);
    }
    return function;
}
