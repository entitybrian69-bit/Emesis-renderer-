#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "mc_version_detector.h"

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef double GLdouble;
#define GL_GLEXT_PROTOTYPES
#include "../emesis_modern/GL/gl.h"
#include "../emesis_modern/GL/glext.h"

#define EMESIS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "EmesisRenderer", __VA_ARGS__)
#define EMESIS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "EmesisRenderer", __VA_ARGS__)

typedef __eglMustCastToProperFunctionPointerType (*pfn_eglGetProcAddress_t)(const char*);
typedef void* (*pfn_glXGetProcAddress_t)(const char*);
typedef void (*pfn_getmainfbsize_cb_t)(int*, int*);
typedef void* (*pfn_getprocaddress_cb_t)(const char*);

static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;
static emesis_route_decision_t g_decision;
static void* g_engine_handle = NULL;
static void* g_sys_egl_handle = NULL;
static pfn_eglGetProcAddress_t g_sys_eglGetProcAddress = NULL;
static pfn_eglGetProcAddress_t g_modern_eglGetProcAddress = NULL;
static pfn_glXGetProcAddress_t g_legacy_glXGetProcAddress = NULL;

/* Launcher bridge callbacks registered prior to deferred engine open */
static pfn_getmainfbsize_cb_t g_saved_fbsize_cb = NULL;
static pfn_getprocaddress_cb_t g_saved_procaddr_cb = NULL;

/* Core system EGL function pointers */
static EGLDisplay (*g_eglGetDisplay)(EGLNativeDisplayType) = NULL;
static EGLBoolean (*g_eglInitialize)(EGLDisplay, EGLint*, EGLint*) = NULL;
static EGLBoolean (*g_eglTerminate)(EGLDisplay) = NULL;
static const char* (*g_eglQueryString)(EGLDisplay, EGLint) = NULL;
static EGLBoolean (*g_eglGetConfigs)(EGLDisplay, EGLConfig*, EGLint, EGLint*) = NULL;
static EGLBoolean (*g_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = NULL;
static EGLBoolean (*g_eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint*) = NULL;
static EGLSurface (*g_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) = NULL;
static EGLSurface (*g_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint*) = NULL;
static EGLSurface (*g_eglCreatePixmapSurface)(EGLDisplay, EGLConfig, EGLNativePixmapType, const EGLint*) = NULL;
static EGLBoolean (*g_eglDestroySurface)(EGLDisplay, EGLSurface) = NULL;
static EGLBoolean (*g_eglQuerySurface)(EGLDisplay, EGLSurface, EGLint, EGLint*) = NULL;
static EGLBoolean (*g_eglBindAPI)(EGLenum) = NULL;
static EGLenum (*g_eglQueryAPI)(void) = NULL;
static EGLBoolean (*g_eglWaitClient)(void) = NULL;
static EGLBoolean (*g_eglReleaseThread)(void) = NULL;
static EGLBoolean (*g_eglSurfaceAttrib)(EGLDisplay, EGLSurface, EGLint, EGLint) = NULL;
static EGLBoolean (*g_eglBindTexImage)(EGLDisplay, EGLSurface, EGLint) = NULL;
static EGLBoolean (*g_eglReleaseTexImage)(EGLDisplay, EGLSurface, EGLint) = NULL;
static EGLBoolean (*g_eglSwapInterval)(EGLDisplay, EGLint) = NULL;
static EGLContext (*g_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) = NULL;
static EGLBoolean (*g_eglDestroyContext)(EGLDisplay, EGLContext) = NULL;
static EGLBoolean (*g_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = NULL;
static EGLContext (*g_eglGetCurrentContext)(void) = NULL;
static EGLSurface (*g_eglGetCurrentSurface)(EGLint) = NULL;
static EGLDisplay (*g_eglGetCurrentDisplay)(void) = NULL;
static EGLBoolean (*g_eglQueryContext)(EGLDisplay, EGLContext, EGLint, EGLint*) = NULL;
static EGLBoolean (*g_eglWaitGL)(void) = NULL;
static EGLBoolean (*g_eglWaitNative)(EGLint) = NULL;
static EGLBoolean (*g_eglSwapBuffers)(EGLDisplay, EGLSurface) = NULL;
static EGLBoolean (*g_eglCopyBuffers)(EGLDisplay, EGLSurface, EGLNativePixmapType) = NULL;
static EGLint (*g_eglGetError)(void) = NULL;

/* Optional Modern engine EGL overrides */
static EGLContext (*g_mod_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) = NULL;
static EGLBoolean (*g_mod_eglDestroyContext)(EGLDisplay, EGLContext) = NULL;
static EGLBoolean (*g_mod_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = NULL;

static void emesis_load_sys_egl(void) {
    if (g_sys_egl_handle) return;
    g_sys_egl_handle = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
    if (!g_sys_egl_handle) {
        g_sys_egl_handle = dlopen("/system/lib64/libEGL.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (!g_sys_egl_handle) {
        g_sys_egl_handle = dlopen("/system/lib/libEGL.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (!g_sys_egl_handle) {
        EMESIS_LOGE("Failed to open Android system libEGL.so: %s", dlerror());
        return;
    }

#define LOAD_SYS_EGL(fn) g_##fn = (__typeof__(g_##fn))dlsym(g_sys_egl_handle, #fn)
    LOAD_SYS_EGL(eglGetDisplay);
    LOAD_SYS_EGL(eglInitialize);
    LOAD_SYS_EGL(eglTerminate);
    LOAD_SYS_EGL(eglQueryString);
    LOAD_SYS_EGL(eglGetConfigs);
    LOAD_SYS_EGL(eglChooseConfig);
    LOAD_SYS_EGL(eglGetConfigAttrib);
    LOAD_SYS_EGL(eglCreateWindowSurface);
    LOAD_SYS_EGL(eglCreatePbufferSurface);
    LOAD_SYS_EGL(eglCreatePixmapSurface);
    LOAD_SYS_EGL(eglDestroySurface);
    LOAD_SYS_EGL(eglQuerySurface);
    LOAD_SYS_EGL(eglBindAPI);
    LOAD_SYS_EGL(eglQueryAPI);
    LOAD_SYS_EGL(eglWaitClient);
    LOAD_SYS_EGL(eglReleaseThread);
    LOAD_SYS_EGL(eglSurfaceAttrib);
    LOAD_SYS_EGL(eglBindTexImage);
    LOAD_SYS_EGL(eglReleaseTexImage);
    LOAD_SYS_EGL(eglSwapInterval);
    LOAD_SYS_EGL(eglCreateContext);
    LOAD_SYS_EGL(eglDestroyContext);
    LOAD_SYS_EGL(eglMakeCurrent);
    LOAD_SYS_EGL(eglGetCurrentContext);
    LOAD_SYS_EGL(eglGetCurrentSurface);
    LOAD_SYS_EGL(eglGetCurrentDisplay);
    LOAD_SYS_EGL(eglQueryContext);
    LOAD_SYS_EGL(eglWaitGL);
    LOAD_SYS_EGL(eglWaitNative);
    LOAD_SYS_EGL(eglSwapBuffers);
    LOAD_SYS_EGL(eglCopyBuffers);
    LOAD_SYS_EGL(eglGetError);
#undef LOAD_SYS_EGL
    g_sys_eglGetProcAddress = (pfn_eglGetProcAddress_t)dlsym(g_sys_egl_handle, "eglGetProcAddress");
}

static void emesis_init_engine_once(void) {
    emesis_load_sys_egl();
    emesis_detect_runtime_route(&g_decision);

    const char* target_soname = (g_decision.engine == EMESIS_ENGINE_MODERN)
                                    ? "libemesis_modern.so"
                                    : "libemesis_legacy.so";

    char full_path[1024] = {0};
    Dl_info self_info;
    if (dladdr((void*)&emesis_init_engine_once, &self_info) && self_info.dli_fname) {
        const char* slash = strrchr(self_info.dli_fname, '/');
        if (slash) {
            size_t dir_len = (size_t)(slash - self_info.dli_fname);
            if (dir_len + strlen(target_soname) + 2 < sizeof(full_path)) {
                memcpy(full_path, self_info.dli_fname, dir_len);
                full_path[dir_len] = '/';
                strcpy(full_path + dir_len + 1, target_soname);
            }
        }
    }

    /* Isolate backend symbols via RTLD_LOCAL so Modern and Legacy never collide! */
    if (full_path[0]) {
        g_engine_handle = dlopen(full_path, RTLD_LAZY | RTLD_LOCAL);
    }
    if (!g_engine_handle) {
        g_engine_handle = dlopen(target_soname, RTLD_LAZY | RTLD_LOCAL);
    }

    if (!g_engine_handle) {
        EMESIS_LOGE("Failed to load %s (%s): %s", target_soname, full_path, dlerror());
        return;
    }

    EMESIS_LOGI("Emesis Renderer v1.0.0 (Made by Entitybrian) -> Selected [%s] for MC [%s] via [%s]",
                target_soname, g_decision.detected_version, g_decision.detection_source);

    if (g_decision.engine == EMESIS_ENGINE_MODERN) {
        g_modern_eglGetProcAddress = (pfn_eglGetProcAddress_t)dlsym(g_engine_handle, "eglGetProcAddress");
        g_mod_eglCreateContext = (__typeof__(g_mod_eglCreateContext))dlsym(g_engine_handle, "eglCreateContext");
        g_mod_eglDestroyContext = (__typeof__(g_mod_eglDestroyContext))dlsym(g_engine_handle, "eglDestroyContext");
        g_mod_eglMakeCurrent = (__typeof__(g_mod_eglMakeCurrent))dlsym(g_engine_handle, "eglMakeCurrent");
    } else {
        g_legacy_glXGetProcAddress = (pfn_glXGetProcAddress_t)dlsym(g_engine_handle, "glXGetProcAddress");
        if (g_saved_fbsize_cb) {
            void (*set_fb)(pfn_getmainfbsize_cb_t) =
                (void (*)(pfn_getmainfbsize_cb_t))dlsym(g_engine_handle, "set_getmainfbsize");
            if (set_fb) set_fb(g_saved_fbsize_cb);
        }
        if (g_saved_procaddr_cb) {
            void (*set_pa)(pfn_getprocaddress_cb_t) =
                (void (*)(pfn_getprocaddress_cb_t))dlsym(g_engine_handle, "set_getprocaddress");
            if (set_pa) set_pa(g_saved_procaddr_cb);
        }
        void (*init_fn)(void) = (void (*)(void))dlsym(g_engine_handle, "initialize_gl4es");
        if (init_fn) init_fn();
    }
}

static inline void emesis_ensure_engine(void) {
    pthread_once(&g_init_once, emesis_init_engine_once);
}

static void emesis_noop_stub(void) {}

void* emesis_resolve_gl_symbol(const char* name) {
    if (!name || !*name) return NULL;
    emesis_ensure_engine();
    if (!g_engine_handle) return (void*)&emesis_noop_stub;

    void* sym = dlsym(g_engine_handle, name);
    if (sym) return sym;

    if (g_decision.engine == EMESIS_ENGINE_MODERN && g_modern_eglGetProcAddress) {
        sym = (void*)g_modern_eglGetProcAddress(name);
        if (sym) return sym;
    } else if (g_decision.engine == EMESIS_ENGINE_LEGACY && g_legacy_glXGetProcAddress) {
        sym = g_legacy_glXGetProcAddress(name);
        if (sym) return sym;
    }

    if (g_sys_eglGetProcAddress) {
        sym = (void*)g_sys_eglGetProcAddress(name);
        if (sym) return sym;
    }
    return (void*)&emesis_noop_stub;
}

/* Launcher bridge callbacks (Pojav / FCL / Zalith) */
__attribute__((visibility("default"))) void set_getmainfbsize(pfn_getmainfbsize_cb_t cb) {
    g_saved_fbsize_cb = cb;
    if (g_engine_handle && g_decision.engine == EMESIS_ENGINE_LEGACY) {
        void (*set_fb)(pfn_getmainfbsize_cb_t) =
            (void (*)(pfn_getmainfbsize_cb_t))dlsym(g_engine_handle, "set_getmainfbsize");
        if (set_fb) set_fb(cb);
    }
}

__attribute__((visibility("default"))) void set_getprocaddress(pfn_getprocaddress_cb_t cb) {
    g_saved_procaddr_cb = cb;
    if (g_engine_handle && g_decision.engine == EMESIS_ENGINE_LEGACY) {
        void (*set_pa)(pfn_getprocaddress_cb_t) =
            (void (*)(pfn_getprocaddress_cb_t))dlsym(g_engine_handle, "set_getprocaddress");
        if (set_pa) set_pa(cb);
    }
}

/* Exported EGL Functions (called directly OR via eglGetProcAddress("egl...") by FCL/Zalith egl_loader.c) */
__attribute__((visibility("default"))) EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
    emesis_load_sys_egl();
    return g_eglGetDisplay ? g_eglGetDisplay(display_id) : EGL_NO_DISPLAY;
}

__attribute__((visibility("default"))) EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    emesis_load_sys_egl();
    return g_eglInitialize ? g_eglInitialize(dpy, major, minor) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglTerminate(EGLDisplay dpy) {
    emesis_load_sys_egl();
    return g_eglTerminate ? g_eglTerminate(dpy) : EGL_FALSE;
}

__attribute__((visibility("default"))) const char* eglQueryString(EGLDisplay dpy, EGLint name) {
    emesis_load_sys_egl();
    return g_eglQueryString ? g_eglQueryString(dpy, name) : NULL;
}

__attribute__((visibility("default"))) EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig* configs, EGLint config_size, EGLint* num_config) {
    emesis_load_sys_egl();
    return g_eglGetConfigs ? g_eglGetConfigs(dpy, configs, config_size, num_config) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size, EGLint* num_config) {
    emesis_load_sys_egl();
    if (!g_eglChooseConfig) return EGL_FALSE;
    EGLBoolean ok = g_eglChooseConfig(dpy, attrib_list, configs, config_size, num_config);
    /* Automatic fallback for older 32-bit GLES 2.0-only GPUs if EGL_OPENGL_ES3_BIT_KHR was requested */
    if ((!ok || (num_config && *num_config == 0)) && attrib_list) {
        EGLint patched[64];
        int n = 0;
        bool changed = false;
        while (attrib_list[n] != EGL_NONE && n < 62) {
            patched[n] = attrib_list[n];
            patched[n + 1] = attrib_list[n + 1];
            if (patched[n] == EGL_RENDERABLE_TYPE && (patched[n + 1] & 0x0040)) {
                patched[n + 1] = EGL_OPENGL_ES2_BIT;
                changed = true;
            }
            n += 2;
        }
        patched[n] = EGL_NONE;
        if (changed) {
            ok = g_eglChooseConfig(dpy, patched, configs, config_size, num_config);
        }
    }
    return ok;
}

__attribute__((visibility("default"))) EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value) {
    emesis_load_sys_egl();
    return g_eglGetConfigAttrib ? g_eglGetConfigAttrib(dpy, config, attribute, value) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint* attrib_list) {
    emesis_load_sys_egl();
    return g_eglCreateWindowSurface ? g_eglCreateWindowSurface(dpy, config, win, attrib_list) : EGL_NO_SURFACE;
}

__attribute__((visibility("default"))) EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list) {
    emesis_load_sys_egl();
    return g_eglCreatePbufferSurface ? g_eglCreatePbufferSurface(dpy, config, attrib_list) : EGL_NO_SURFACE;
}

__attribute__((visibility("default"))) EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint* attrib_list) {
    emesis_load_sys_egl();
    return g_eglCreatePixmapSurface ? g_eglCreatePixmapSurface(dpy, config, pixmap, attrib_list) : EGL_NO_SURFACE;
}

__attribute__((visibility("default"))) EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    emesis_load_sys_egl();
    return g_eglDestroySurface ? g_eglDestroySurface(dpy, surface) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint* value) {
    emesis_load_sys_egl();
    return g_eglQuerySurface ? g_eglQuerySurface(dpy, surface, attribute, value) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglBindAPI(EGLenum api) {
    emesis_load_sys_egl();
    return g_eglBindAPI ? g_eglBindAPI(api) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLenum eglQueryAPI(void) {
    emesis_load_sys_egl();
    return g_eglQueryAPI ? g_eglQueryAPI() : EGL_OPENGL_ES_API;
}

__attribute__((visibility("default"))) EGLBoolean eglWaitClient(void) {
    emesis_load_sys_egl();
    return g_eglWaitClient ? g_eglWaitClient() : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglReleaseThread(void) {
    emesis_load_sys_egl();
    return g_eglReleaseThread ? g_eglReleaseThread() : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value) {
    emesis_load_sys_egl();
    return g_eglSurfaceAttrib ? g_eglSurfaceAttrib(dpy, surface, attribute, value) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    emesis_load_sys_egl();
    return g_eglBindTexImage ? g_eglBindTexImage(dpy, surface, buffer) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    emesis_load_sys_egl();
    return g_eglReleaseTexImage ? g_eglReleaseTexImage(dpy, surface, buffer) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    emesis_load_sys_egl();
    return g_eglSwapInterval ? g_eglSwapInterval(dpy, interval) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint* attrib_list) {
    emesis_ensure_engine();
    if (g_decision.engine == EMESIS_ENGINE_MODERN && g_mod_eglCreateContext) {
        return g_mod_eglCreateContext(dpy, config, share_context, attrib_list);
    }
    if (!g_eglCreateContext) return EGL_NO_CONTEXT;
    EGLContext ctx = g_eglCreateContext(dpy, config, share_context, attrib_list);
    /* Fallback to GLES 2 context for older 32-bit GPUs on MC <= 1.16.5 */
    if (ctx == EGL_NO_CONTEXT && g_decision.engine == EMESIS_ENGINE_LEGACY) {
        const EGLint es2_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        setenv("LIBGL_ES", "2", 1);
        ctx = g_eglCreateContext(dpy, config, share_context, es2_attribs);
    }
    return ctx;
}

__attribute__((visibility("default"))) EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    emesis_ensure_engine();
    if (g_decision.engine == EMESIS_ENGINE_MODERN && g_mod_eglDestroyContext) {
        return g_mod_eglDestroyContext(dpy, ctx);
    }
    return g_eglDestroyContext ? g_eglDestroyContext(dpy, ctx) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    emesis_ensure_engine();
    if (g_decision.engine == EMESIS_ENGINE_MODERN && g_mod_eglMakeCurrent) {
        return g_mod_eglMakeCurrent(dpy, draw, read, ctx);
    }
    return g_eglMakeCurrent ? g_eglMakeCurrent(dpy, draw, read, ctx) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLContext eglGetCurrentContext(void) {
    emesis_load_sys_egl();
    return g_eglGetCurrentContext ? g_eglGetCurrentContext() : EGL_NO_CONTEXT;
}

__attribute__((visibility("default"))) EGLSurface eglGetCurrentSurface(EGLint readdraw) {
    emesis_load_sys_egl();
    return g_eglGetCurrentSurface ? g_eglGetCurrentSurface(readdraw) : EGL_NO_SURFACE;
}

__attribute__((visibility("default"))) EGLDisplay eglGetCurrentDisplay(void) {
    emesis_load_sys_egl();
    return g_eglGetCurrentDisplay ? g_eglGetCurrentDisplay() : EGL_NO_DISPLAY;
}

__attribute__((visibility("default"))) EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint* value) {
    emesis_load_sys_egl();
    return g_eglQueryContext ? g_eglQueryContext(dpy, ctx, attribute, value) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglWaitGL(void) {
    emesis_load_sys_egl();
    return g_eglWaitGL ? g_eglWaitGL() : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglWaitNative(EGLint engine) {
    emesis_load_sys_egl();
    return g_eglWaitNative ? g_eglWaitNative(engine) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    emesis_load_sys_egl();
    return g_eglSwapBuffers ? g_eglSwapBuffers(dpy, surface) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target) {
    emesis_load_sys_egl();
    return g_eglCopyBuffers ? g_eglCopyBuffers(dpy, surface, target) : EGL_FALSE;
}

__attribute__((visibility("default"))) EGLint eglGetError(void) {
    emesis_load_sys_egl();
    return g_eglGetError ? g_eglGetError() : EGL_SUCCESS;
}

__attribute__((visibility("default"))) __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* procname) {
    if (!procname) return NULL;
    if (strncmp(procname, "egl", 3) == 0) {
#define MATCH_EGL(fn) if (strcmp(procname, #fn) == 0) return (__eglMustCastToProperFunctionPointerType)&fn
        MATCH_EGL(eglGetDisplay);
        MATCH_EGL(eglInitialize);
        MATCH_EGL(eglTerminate);
        MATCH_EGL(eglQueryString);
        MATCH_EGL(eglGetConfigs);
        MATCH_EGL(eglChooseConfig);
        MATCH_EGL(eglGetConfigAttrib);
        MATCH_EGL(eglCreateWindowSurface);
        MATCH_EGL(eglCreatePbufferSurface);
        MATCH_EGL(eglCreatePixmapSurface);
        MATCH_EGL(eglDestroySurface);
        MATCH_EGL(eglQuerySurface);
        MATCH_EGL(eglBindAPI);
        MATCH_EGL(eglQueryAPI);
        MATCH_EGL(eglWaitClient);
        MATCH_EGL(eglReleaseThread);
        MATCH_EGL(eglSurfaceAttrib);
        MATCH_EGL(eglBindTexImage);
        MATCH_EGL(eglReleaseTexImage);
        MATCH_EGL(eglSwapInterval);
        MATCH_EGL(eglCreateContext);
        MATCH_EGL(eglDestroyContext);
        MATCH_EGL(eglMakeCurrent);
        MATCH_EGL(eglGetCurrentContext);
        MATCH_EGL(eglGetCurrentSurface);
        MATCH_EGL(eglGetCurrentDisplay);
        MATCH_EGL(eglQueryContext);
        MATCH_EGL(eglWaitGL);
        MATCH_EGL(eglWaitNative);
        MATCH_EGL(eglSwapBuffers);
        MATCH_EGL(eglCopyBuffers);
        MATCH_EGL(eglGetError);
        MATCH_EGL(eglGetProcAddress);
#undef MATCH_EGL
        emesis_load_sys_egl();
        return g_sys_eglGetProcAddress ? g_sys_eglGetProcAddress(procname) : NULL;
    }
    return (__eglMustCastToProperFunctionPointerType)emesis_resolve_gl_symbol(procname);
}

__attribute__((visibility("default"))) void* glXGetProcAddress(const char* procname) {
    return (void*)eglGetProcAddress(procname);
}

__attribute__((visibility("default"))) void* glXGetProcAddressARB(const char* procname) {
    return (void*)eglGetProcAddress(procname);
}

/* Typed direct-export OpenGL forwarders */
#include "gl_forwards.inc"

/* Companion App JNI Diagnostics & Version Routing Simulator */
__attribute__((visibility("default"))) JNIEXPORT jstring JNICALL
Java_com_entitybrian_emesis_EmesisNative_getEngineVersion(JNIEnv* env, jclass cls) {
    (void)cls;
    return (*env)->NewStringUTF(env, "Emesis Renderer v1.0.0 — Made by Entitybrian");
}

__attribute__((visibility("default"))) JNIEXPORT jstring JNICALL
Java_com_entitybrian_emesis_EmesisNative_simulateRoute(JNIEnv* env, jclass cls, jstring jver) {
    (void)cls;
    if (!jver) return (*env)->NewStringUTF(env, "Unknown version");
    const char* cver = (*env)->GetStringUTFChars(env, jver, NULL);
    int res = emesis_classify_mc_version_string(cver);
    char msg[256];
    if (res == EMESIS_ENGINE_LEGACY) {
        snprintf(msg, sizeof(msg),
                 "Minecraft %s (<= 1.16.5) -> Emesis Compatibility Core (OpenGL 2.1 / GLSL-SPIRV Pipeline)",
                 cver);
    } else if (res == EMESIS_ENGINE_MODERN) {
        snprintf(msg, sizeof(msg),
                 "Minecraft %s (> 1.16.5) -> Emesis Modern Core (OpenGL 3.2+ Core Pipeline)",
                 cver);
    } else {
        snprintf(msg, sizeof(msg),
                 "Unrecognized tag '%s' -> Auto-inspects client.jar!/version.json & JVM runtime at launch",
                 cver);
    }
    (*env)->ReleaseStringUTFChars(env, jver, cver);
    return (*env)->NewStringUTF(env, msg);
}
