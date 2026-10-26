/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include <stdint.h>
#include "int_hash.h"
#include "../libraryinternal.h"

static size_t intmap_hash(void* key) {
#if UINTPTR_MAX > 0xFFFFFFFFu
    uint64_t x = (uint64_t)(uintptr_t)key;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
#else
    uint32_t x = (uint32_t)(uintptr_t)key;
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return (size_t)x;
#endif
}

static bool intmap_equals(void* v1, void* v2) {
    return v1 == v2;
}

INTERNAL unordered_map* alloc_intmap_safe() {
    return unordered_map_alloc(0, 1, intmap_hash, intmap_equals);
}

INTERNAL unordered_map* alloc_intmap() {
    unordered_map* map = alloc_intmap_safe();
    if(map == NULL) {
        printf("failed to alloc_intmap\n");
        abort();
    }
    return map;
}