#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "mc_version_detector.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef double GLdouble;
#define GL_GLEXT_PROTOTYPES
#include "../emesis_modern/GL/gl.h"
#include "../emesis_modern/GL/glext.h"

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif
#ifndef EGL_CONTEXT_MAJOR_VERSION_KHR
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION_KHR
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#endif

#define EMESIS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "LIBGL", "[EmesisRenderer] " __VA_ARGS__)
#define EMESIS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "LIBGL", "[EmesisRenderer] " __VA_ARGS__)

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
static char g_self_path[1024] = {0};
static char g_self_dir[1024] = {0};

/* Launcher bridge callbacks registered prior to or after deferred engine open */
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
static volatile int g_lwjgl_hijacked = 0;

static void emesis_discover_self_paths(void) {
    if (g_self_dir[0]) return;
    Dl_info self_info;
    if (dladdr((void*)&emesis_discover_self_paths, &self_info) && self_info.dli_fname) {
        snprintf(g_self_path, sizeof(g_self_path), "%s", self_info.dli_fname);
        const char* slash = strrchr(self_info.dli_fname, '/');
        if (slash) {
            size_t dir_len = (size_t)(slash - self_info.dli_fname);
            if (dir_len + 1 < sizeof(g_self_dir)) {
                memcpy(g_self_dir, self_info.dli_fname, dir_len);
                g_self_dir[dir_len] = '\0';
            }
        }
    }
}

static bool emesis_is_gl_candidate_lib(const char* filename) {
    if (!filename || !*filename) return false;
    if (strstr(filename, "emesis")) return false;
    const char* base = strrchr(filename, '/');
    base = base ? (base + 1) : filename;
    if (strncmp(base, "libgl4es", 8) == 0 ||
        strncmp(base, "libng_gl4es", 11) == 0 ||
        strncmp(base, "libtinywrapper", 14) == 0 ||
        strncmp(base, "libGLMojo", 9) == 0 ||
        strncmp(base, "libGLESv2", 9) == 0 ||
        strncmp(base, "libGLESv3", 9) == 0 ||
        strncmp(base, "libGL.so", 8) == 0 ||
        strncmp(base, "libGLX.so", 9) == 0 ||
        strncmp(base, "libvgpu", 7) == 0) {
        return true;
    }
    /* Catch any other built-in renderer library loaded from POJAV_NATIVEDIR */
    const char* pojav_ndir = getenv("POJAV_NATIVEDIR");
    if (pojav_ndir && *pojav_ndir && strncmp(filename, pojav_ndir, strlen(pojav_ndir)) == 0) {
        if (!strstr(base, "openal") && !strstr(base, "glfw") && !strstr(base, "lwjgl") &&
            !strstr(base, "freetype") && !strstr(base, "shaderc") && !strstr(base, "pojav") &&
            !strstr(base, "awt") && !strstr(base, "vulkan")) {
            return true;
        }
    }
    return false;
}

static jlong JNICALL emesis_lwjgl_ndlopen_hook(JNIEnv* env, jclass cls, jlong filename_ptr, jint jmode) {
    (void)env;
    (void)cls;
    const char* filename = (const char*)(uintptr_t)filename_ptr;
    if (!filename) return 0;

    const char* base = strrchr(filename, '/');
    base = base ? (base + 1) : filename;

    /* Preserve custom Vulkan driver loader hooks across Pojav & Mojo variants */
    if (strncmp(base, "libvulkan.so", 12) == 0) {
        void* (*acq_vk)(void) = (void* (*)(void))dlsym(RTLD_DEFAULT, "mojoexec_acq_vulkan_handle");
        if (acq_vk) {
            void* h = acq_vk();
            if (h) return (jlong)(uintptr_t)h;
        }
        void* (*maybe_vk)(void) = (void* (*)(void))dlsym(RTLD_DEFAULT, "maybe_load_vulkan");
        if (maybe_vk) {
            void* h = maybe_vk();
            if (h) return (jlong)(uintptr_t)h;
        }
    }

    if (g_self_path[0] == '/' && emesis_is_gl_candidate_lib(filename)) {
        EMESIS_LOGI("Intercepted LWJGL dlopen('%s') -> '%s'", filename, g_self_path);
        void* self_h = dlopen(g_self_path, RTLD_LAZY | RTLD_GLOBAL);
        if (self_h) return (jlong)(uintptr_t)self_h;
    }

    return (jlong)(uintptr_t)dlopen(filename, (int)jmode);
}

/*
 * Ensures LWJGL 2 & LWJGL 3 bind OpenGL to libemesis_renderer.so whenever Emesis is active as
 * POJAVEXEC_EGL, even if a Pojav variant overwrote -Dorg.lwjgl.opengl.libname on the CLI with
 * libgl4es_114.so, libGLESv2.so, or libGLMojo.so.
 */
static void emesis_hijack_lwjgl_gl_lib(void) {
    if (__atomic_load_n(&g_lwjgl_hijacked, __ATOMIC_ACQUIRE)) return;
    emesis_discover_self_paths();
    if (g_self_path[0] != '/') return;
    if (!getenv("JAVA_HOME") && !getenv("POJAV_RENDERER") && !getenv("POJAVEXEC_EGL")) return;

    typedef jint (*JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);
    JNI_GetCreatedJavaVMs_t get_vms =
        (JNI_GetCreatedJavaVMs_t)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
    if (!get_vms) return;

    JavaVM* vm = NULL;
    jsize count = 0;
    if (get_vms(&vm, 1, &count) != JNI_OK || count <= 0 || !vm) return;

    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) return;
    if ((*env)->ExceptionCheck(env)) return;

    jstring jpath = (*env)->NewStringUTF(env, g_self_path);
    if (!jpath) {
        (*env)->ExceptionClear(env);
        return;
    }

    /* 1. Override System.setProperty("org.lwjgl.opengl.libname", g_self_path) */
    jclass sys_cls = (*env)->FindClass(env, "java/lang/System");
    if (sys_cls) {
        jmethodID set_prop = (*env)->GetStaticMethodID(
            env, sys_cls, "setProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        if (set_prop) {
            jstring jkey = (*env)->NewStringUTF(env, "org.lwjgl.opengl.libname");
            if (jkey) {
                jobject prev = (*env)->CallStaticObjectMethod(env, sys_cls, set_prop, jkey, jpath);
                if (prev) (*env)->DeleteLocalRef(env, prev);
                (*env)->DeleteLocalRef(env, jkey);
            }
        }
        (*env)->DeleteLocalRef(env, sys_cls);
    }
    (*env)->ExceptionClear(env);

    /* 2. Update org.lwjgl.system.Configuration.OPENGL_LIBRARY_NAME.set(g_self_path) if LWJGL3 present */
    jclass cfg_cls = (*env)->FindClass(env, "org/lwjgl/system/Configuration");
    if (cfg_cls) {
        jfieldID gl_lib_fid = (*env)->GetStaticFieldID(
            env, cfg_cls, "OPENGL_LIBRARY_NAME", "Lorg/lwjgl/system/Configuration;");
        jmethodID set_mid = (*env)->GetMethodID(env, cfg_cls, "set", "(Ljava/lang/Object;)V");
        if (gl_lib_fid && set_mid) {
            jobject opt_obj = (*env)->GetStaticObjectField(env, cfg_cls, gl_lib_fid);
            if (opt_obj) {
                (*env)->CallVoidMethod(env, opt_obj, set_mid, jpath);
                (*env)->DeleteLocalRef(env, opt_obj);
            }
        }
        (*env)->DeleteLocalRef(env, cfg_cls);
    }
    (*env)->ExceptionClear(env);

    /* 3. Hook org.lwjgl.system.linux.DynamicLinkLoader.ndlopen(JI)J as a 100% catch-all */
    jclass dll_cls = (*env)->FindClass(env, "org/lwjgl/system/linux/DynamicLinkLoader");
    if (dll_cls) {
        JNINativeMethod hook_method = {
            (char*)"ndlopen",
            (char*)"(JI)J",
            (void*)&emesis_lwjgl_ndlopen_hook
        };
        (*env)->RegisterNatives(env, dll_cls, &hook_method, 1);
        (*env)->DeleteLocalRef(env, dll_cls);
    }
    (*env)->ExceptionClear(env);

    (*env)->DeleteLocalRef(env, jpath);
    __atomic_store_n(&g_lwjgl_hijacked, 1, __ATOMIC_RELEASE);
    EMESIS_LOGI("Bound LWJGL OpenGL library target -> %s", g_self_path);
}

__attribute__((constructor))
static void emesis_router_on_load(void) {
    emesis_discover_self_paths();

    /* Prevent self-referential LIBGL_EGL loops across all Pojav variants */
    const char* libgl_egl = getenv("LIBGL_EGL");
    if (libgl_egl && strstr(libgl_egl, "emesis")) {
        unsetenv("LIBGL_EGL");
    }

    /* Ensure SDL-based LWJGL3 paths in Pojav/Amethyst forks point to our full library path */
    if (g_self_path[0] == '/') {
        const char* pojav_egl = getenv("POJAVEXEC_EGL");
        if (pojav_egl && strstr(pojav_egl, "emesis")) {
            setenv("SDL_OPENGL_LIBRARY", g_self_path, 0);
            setenv("SDL_EGL_LIBRARY", g_self_path, 0);
        }
    }
}

static void emesis_load_sys_egl(void) {
    if (g_sys_egl_handle) return;
#if defined(__aarch64__) || defined(__x86_64__)
    g_sys_egl_handle = dlopen("/system/lib64/libEGL.so", RTLD_LAZY | RTLD_LOCAL);
#else
    g_sys_egl_handle = dlopen("/system/lib/libEGL.so", RTLD_LAZY | RTLD_LOCAL);
#endif
    if (!g_sys_egl_handle) {
        g_sys_egl_handle = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
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

/*
 * Built-in framebuffer dimension query for Pojav variants whose PojavRendererInit
 * only registers set_getmainfbsize for hardcoded built-in filenames.
 */
static void emesis_default_getmainfbsize(int* width, int* height) {
    if (!width || !height) return;
    emesis_load_sys_egl();
    if (g_eglGetCurrentDisplay && g_eglGetCurrentSurface && g_eglQuerySurface) {
        EGLDisplay dpy = g_eglGetCurrentDisplay();
        EGLSurface surf = g_eglGetCurrentSurface(EGL_DRAW);
        if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE) {
            EGLint w = 0, h = 0;
            if (g_eglQuerySurface(dpy, surf, EGL_WIDTH, &w) == EGL_TRUE &&
                g_eglQuerySurface(dpy, surf, EGL_HEIGHT, &h) == EGL_TRUE &&
                w > 0 && h > 0) {
                *width = (int)w;
                *height = (int)h;
                return;
            }
        }
    }
    const char* awt_w = getenv("AWTSTUB_WIDTH");
    const char* awt_h = getenv("AWTSTUB_HEIGHT");
    if (awt_w && awt_h) {
        int w = atoi(awt_w);
        int h = atoi(awt_h);
        if (w > 0 && h > 0) {
            *width = w;
            *height = h;
            return;
        }
    }
}

static void emesis_init_engine_once(void) {
    emesis_discover_self_paths();
    emesis_load_sys_egl();
    emesis_detect_runtime_route(&g_decision);

    const char* target_soname = (g_decision.engine == EMESIS_ENGINE_MODERN)
                                    ? "libemesis_modern.so"
                                    : "libemesis_legacy.so";

    /* Promote Legacy (<= 1.16.5) to GLES 3.x even when PojavLauncher sets LIBGL_ES=2 by default,
       unlocking ESSL 3.20/3.10/3.00 shaders, MRT drawbuffers, shadow2D, texelFetch, and glProgramBinary. */
    if (g_decision.engine == EMESIS_ENGINE_LEGACY) {
        const char* force_es2 = getenv("EMESIS_FORCE_GLES2");
        if (!force_es2 || strcmp(force_es2, "1") != 0) {
            setenv("LIBGL_ES", "3", 1);
        }
        /* Ensure bare V1 launcher forks that skip pojavEnv still disable synchronous glGetError
           stalls and report OpenGL 3.1 capability to Sodium/Iris/OptiFine. */
        setenv("LIBGL_GL", "31", 0);
        setenv("LIBGL_NOERROR", "1", 0);
        setenv("LIBGL_MIPMAP", "3", 0);
        setenv("LIBGL_NOINTOVLHACK", "1", 0);
        const char* cache_dir = getenv("EMESIS_SHADER_CACHE_DIR");
        bool cache_ok = false;
        if (cache_dir && *cache_dir) {
            mkdir(cache_dir, 0775);
            cache_ok = (access(cache_dir, W_OK | X_OK) == 0);
        }
        if (!cache_ok) {
            const char* tmp_dir = getenv("TMPDIR");
            if (tmp_dir && *tmp_dir) {
                char fallback_cache[1024];
                snprintf(fallback_cache, sizeof(fallback_cache), "%s/emesis_shadercache", tmp_dir);
                mkdir(fallback_cache, 0775);
                if (access(fallback_cache, W_OK | X_OK) == 0) {
                    setenv("EMESIS_SHADER_CACHE_DIR", fallback_cache, 1);
                }
            }
        }
    }

    char full_path[1024] = {0};
    if (g_self_dir[0]) {
        snprintf(full_path, sizeof(full_path), "%s/%s", g_self_dir, target_soname);
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

    EMESIS_LOGI("Emesis Renderer v1.1.6 (Made by Entitybrian) -> Selected [%s] for MC [%s] via [%s]",
                target_soname, g_decision.detected_version, g_decision.detection_source);

    if (g_decision.engine == EMESIS_ENGINE_MODERN) {
        setenv("LIBGL_ES", "3", 1);
        g_modern_eglGetProcAddress = (pfn_eglGetProcAddress_t)dlsym(g_engine_handle, "eglGetProcAddress");
        g_mod_eglCreateContext = (__typeof__(g_mod_eglCreateContext))dlsym(g_engine_handle, "eglCreateContext");
        g_mod_eglDestroyContext = (__typeof__(g_mod_eglDestroyContext))dlsym(g_engine_handle, "eglDestroyContext");
        g_mod_eglMakeCurrent = (__typeof__(g_mod_eglMakeCurrent))dlsym(g_engine_handle, "eglMakeCurrent");
    } else {
        g_legacy_glXGetProcAddress = (pfn_glXGetProcAddress_t)dlsym(g_engine_handle, "glXGetProcAddress");
        void (*set_fb)(pfn_getmainfbsize_cb_t) =
            (void (*)(pfn_getmainfbsize_cb_t))dlsym(g_engine_handle, "set_getmainfbsize");
        if (set_fb) {
            set_fb(g_saved_fbsize_cb ? g_saved_fbsize_cb : &emesis_default_getmainfbsize);
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

/*
 * Dynamic ProcAddress lookup for glXGetProcAddress / glXGetProcAddressARB / eglGetProcAddress.
 * Returns NULL when the active backend does not implement `name`, so LWJGL GLCapabilities,
 * Sodium, Iris, OptiFine, Angelica, and Oculus never enable unsupported GL 4.x / DSA / compute paths.
 */
static void* emesis_query_gl_proc(const char* name) {
    if (!name || !*name) return NULL;
    emesis_ensure_engine();
    if (!g_engine_handle) return NULL;

    if (g_decision.engine == EMESIS_ENGINE_MODERN) {
        if (g_modern_eglGetProcAddress) {
            void* sym = (void*)g_modern_eglGetProcAddress(name);
            if (sym) return sym;
        }
        return dlsym(g_engine_handle, name);
    }

    /* EMESIS_ENGINE_LEGACY (<= 1.16.5): query Legacy engine strictly without falling back to raw GLES */
    if (g_legacy_glXGetProcAddress) {
        void* sym = g_legacy_glXGetProcAddress(name);
        if (sym) return sym;
    }
    return dlsym(g_engine_handle, name);
}

/* Direct C export resolver used by universal core forwarders in gl_forwards.inc */
void* emesis_resolve_gl_symbol(const char* name) {
    void* sym = emesis_query_gl_proc(name);
    if (sym) return sym;
    if (g_decision.engine == EMESIS_ENGINE_MODERN && g_sys_eglGetProcAddress) {
        sym = (void*)g_sys_eglGetProcAddress(name);
        if (sym) return sym;
    }
    return (void*)&emesis_noop_stub;
}

/* Launcher bridge callbacks (all Pojav / FCL / Zalith variants) */
__attribute__((visibility("default"))) void set_getmainfbsize(pfn_getmainfbsize_cb_t cb) {
    g_saved_fbsize_cb = cb;
    if (g_engine_handle && g_decision.engine == EMESIS_ENGINE_LEGACY) {
        void (*set_fb)(pfn_getmainfbsize_cb_t) =
            (void (*)(pfn_getmainfbsize_cb_t))dlsym(g_engine_handle, "set_getmainfbsize");
        if (set_fb) set_fb(cb ? cb : &emesis_default_getmainfbsize);
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

__attribute__((visibility("default"))) void initialize_gl4es(void) {
    emesis_ensure_engine();
    emesis_hijack_lwjgl_gl_lib();
}

/* Exported EGL Functions (called directly OR via eglGetProcAddress("egl...") by Pojav/FCL/Zalith egl_loader.c) */
__attribute__((visibility("default"))) EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
    emesis_load_sys_egl();
    emesis_hijack_lwjgl_gl_lib();
    return g_eglGetDisplay ? g_eglGetDisplay(display_id) : EGL_NO_DISPLAY;
}

__attribute__((visibility("default"))) EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    emesis_load_sys_egl();
    emesis_hijack_lwjgl_gl_lib();
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

    /* Prefer ES3-capable configs when Pojav's gl_bridge requests EGL_OPENGL_ES2_BIT, with seamless ES2 fallback for 32-bit GPUs */
    if (attrib_list) {
        int count = 0;
        int rt_val_idx = -1;
        while (attrib_list[count] != EGL_NONE && count < 60) {
            if (attrib_list[count] == EGL_RENDERABLE_TYPE && attrib_list[count + 1] == EGL_OPENGL_ES2_BIT) {
                rt_val_idx = count + 1;
            }
            count += 2;
        }
        if (rt_val_idx >= 0) {
            EGLint upgraded[64];
            for (int i = 0; i <= count; i++) upgraded[i] = attrib_list[i];
            upgraded[rt_val_idx] = EGL_OPENGL_ES3_BIT_KHR;
            EGLint matched = 0;
            if (g_eglChooseConfig(dpy, upgraded, configs, config_size, &matched) == EGL_TRUE && matched > 0) {
                if (num_config) *num_config = matched;
                return EGL_TRUE;
            }
        }
    }

    return g_eglChooseConfig(dpy, attrib_list, configs, config_size, num_config);
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
    /* Always bind EGL_OPENGL_ES_API on Android hardware drivers even if a desktop-GL Pojav mode requested EGL_OPENGL_API */
    if (api == EGL_OPENGL_API) {
        api = EGL_OPENGL_ES_API;
    }
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
        /*
         * Promote context request to OpenGL ES 3.2 -> 3.1 -> 3.0 even if a PojavLauncher
         * variant passed LIBGL_ES=2 / EGL_CONTEXT_CLIENT_VERSION=2!
         */
        const EGLint es32_attribs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 2, EGL_NONE};
        const EGLint es31_attribs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 1, EGL_NONE};
        const EGLint es30_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

        EGLContext ctx = g_mod_eglCreateContext(dpy, config, share_context, es32_attribs);
        if (ctx != EGL_NO_CONTEXT) return ctx;
        ctx = g_mod_eglCreateContext(dpy, config, share_context, es31_attribs);
        if (ctx != EGL_NO_CONTEXT) return ctx;
        ctx = g_mod_eglCreateContext(dpy, config, share_context, es30_attribs);
        if (ctx != EGL_NO_CONTEXT) return ctx;
        return g_mod_eglCreateContext(dpy, config, share_context, attrib_list);
    }

    if (!g_eglCreateContext) return EGL_NO_CONTEXT;
    const char* force_es2 = getenv("EMESIS_FORCE_GLES2");
    if (!force_es2 || strcmp(force_es2, "1") != 0) {
        const EGLint es32_attribs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 2, EGL_NONE};
        const EGLint es31_attribs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 1, EGL_NONE};
        const EGLint es30_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

        EGLContext ctx3 = g_eglCreateContext(dpy, config, share_context, es32_attribs);
        if (ctx3 != EGL_NO_CONTEXT) return ctx3;
        ctx3 = g_eglCreateContext(dpy, config, share_context, es31_attribs);
        if (ctx3 != EGL_NO_CONTEXT) return ctx3;
        ctx3 = g_eglCreateContext(dpy, config, share_context, es30_attribs);
        if (ctx3 != EGL_NO_CONTEXT) return ctx3;
    }

    const EGLint es3_default[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = g_eglCreateContext(dpy, config, share_context, attrib_list ? attrib_list : es3_default);
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
    emesis_hijack_lwjgl_gl_lib();
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
    if (strcmp(procname, "set_getmainfbsize") == 0) {
        return (__eglMustCastToProperFunctionPointerType)&set_getmainfbsize;
    }
    if (strcmp(procname, "set_getprocaddress") == 0) {
        return (__eglMustCastToProperFunctionPointerType)&set_getprocaddress;
    }
    if (strcmp(procname, "initialize_gl4es") == 0) {
        return (__eglMustCastToProperFunctionPointerType)&initialize_gl4es;
    }
    return (__eglMustCastToProperFunctionPointerType)emesis_query_gl_proc(procname);
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
    return (*env)->NewStringUTF(env, "Emesis Renderer v1.1.6 (Shader Accelerator) — Made by Entitybrian");
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
                 "Unrecognized tag '%s' -> Auto-inspects client.jar & JVM runtime at launch",
                 cver);
    }
    (*env)->ReleaseStringUTFChars(env, jver, cver);
    return (*env)->NewStringUTF(env, msg);
}
