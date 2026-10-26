#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "env.h"
#include "unordered_map/unordered_map.h"

static bool s_stub_debug_checked = false;
static bool s_stub_debug = false;
static unordered_map* s_stub_map = NULL;

static size_t stub_str_hash(void* key) {
    const unsigned char* s = (const unsigned char*)key;
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint32_t)(*s++);
        h *= 16777619u;
    }
    return (size_t)h;
}

static bool stub_str_eq(void* a, void* b) {
    return strcmp((const char*)a, (const char*)b) == 0;
}

#define STUBFUNC(name) \
static bool trigger_##name = false; \
static void stub_##name() { \
    if(trigger_##name) return; \
    trigger_##name = true; \
    if(!s_stub_debug_checked) { \
        s_stub_debug = env_istrue("EMESIS_DEBUG"); \
        s_stub_debug_checked = true; \
    } \
    if(s_stub_debug) printf("EmesisRenderer Stub: "#name"\n"); \
}
#include "stubdefs.h"
#undef STUBFUNC

void init_stub_map(void) {
    if (s_stub_map != NULL) return;
    s_stub_map = unordered_map_alloc(2048, 0.75f, stub_str_hash, stub_str_eq);
    if (s_stub_map == NULL) return;
#define STUBFUNC(name) unordered_map_put(s_stub_map, (void*)#name, (void*)stub_##name);
#include "stubdefs.h"
#undef STUBFUNC
}

void* resolve_stub(const char* procname) {
    if (procname == NULL) return NULL;
    if (s_stub_map != NULL) {
        return unordered_map_get(s_stub_map, (void*)procname);
    }
#define STUBFUNC(name) \
    if(!strcmp(procname, #name)) {                                \
        return stub_##name;     \
    }
#include "stubdefs.h"
#undef STUBFUNC
    return NULL;
}