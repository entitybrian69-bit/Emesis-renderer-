#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include "../gl/attributes.h"
#include "../gl/init.h"
#include "../gl/logs.h"

#define STUB_FCT glXStub
#include "../gl/gl_lookup.h"

#include "glx.h"
#include "hardext.h"

void glXStub(void* x, ...) {
    return;
}

void* gl4es_glXGetProcAddress(const char* name) __attribute__((visibility("default")));

static void* emesis_legacy_self_handle(void) {
    static void* cached = NULL;
    if (!cached) {
        Dl_info info;
        if (dladdr((void*)&gl4es_glXGetProcAddress, &info) && info.dli_fname) {
            cached = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD);
        }
    }
    return cached;
}

void* gl4es_glXGetProcAddress(const char* name) {
    if (!name || !name[0]) return NULL;
    void* self = emesis_legacy_self_handle();
    void* proc = self ? dlsym(self, name) : NULL;
    if (!proc) {
        proc = gl4es_GetProcAddress(name);
    }
    if (!proc) {
        DBG(SHUT_LOGD("[WARNING] Failed to get OpenGL function: %s", name));
        return NULL;
    }
    return proc;
}
#ifndef AMIGAOS4
AliasExport(void*, glXGetProcAddress, , (const char* name));
AliasExport(void*, glXGetProcAddress, ARB, (const char* name));
#endif
