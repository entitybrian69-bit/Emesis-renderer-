//
// Created by whbex on 25.11.2025.
//

#include <GL/gl.h>
#include <GL/glext.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "egl.h"
#include "proc.h"

#define FAKE_TS_RING 64
static thread_local GLuint s_fake_ts_ids[FAKE_TS_RING];
static thread_local uint64_t s_fake_ts_vals[FAKE_TS_RING];
static thread_local unsigned int s_fake_ts_cursor = 0;

static inline uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
    return 1ULL;
}

static void record_fake_ts(GLuint id) {
    if (id == 0) return;
    uint64_t now = monotonic_ns();
    for (unsigned int i = 0; i < FAKE_TS_RING; i++) {
        if (s_fake_ts_ids[i] == id) {
            s_fake_ts_vals[i] = now;
            return;
        }
    }
    unsigned int idx = s_fake_ts_cursor & (FAKE_TS_RING - 1);
    s_fake_ts_ids[idx] = id;
    s_fake_ts_vals[idx] = now;
    s_fake_ts_cursor++;
}

static bool lookup_fake_ts(GLuint id, uint64_t* out_val) {
    if (id == 0) return false;
    for (unsigned int i = 0; i < FAKE_TS_RING; i++) {
        if (s_fake_ts_ids[i] == id) {
            if (out_val) *out_val = s_fake_ts_vals[i];
            return true;
        }
    }
    return false;
}

void glGetQueryObjecti64v(GLuint id, GLenum pname, int64_t* params){
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    if(!ctx->timer_query || !es3_functions.glGetQueryObjecti64vEXT){
        if (pname == GL_QUERY_RESULT_AVAILABLE) {
            *params = 1;
            return;
        }
        uint64_t ts = 0;
        *params = lookup_fake_ts(id, &ts) ? (int64_t)ts : (int64_t)monotonic_ns();
        return;
    }
    es3_functions.glGetQueryObjecti64vEXT(id, pname, params);
}

void glGetQueryObjectui64v(GLuint id, GLenum pname, uint64_t* params){
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    if(!ctx->timer_query || !es3_functions.glGetQueryObjectui64vEXT){
        if (pname == GL_QUERY_RESULT_AVAILABLE) {
            *params = 1;
            return;
        }
        uint64_t ts = 0;
        *params = lookup_fake_ts(id, &ts) ? ts : monotonic_ns();
        return;
    }
    es3_functions.glGetQueryObjectui64vEXT(id, pname, params);
}

void glQueryCounter(GLuint id, GLenum target){
    context_t* ctx = emesis_get_context();
    if(!ctx) return;
    if(!ctx->timer_query || !es3_functions.glQueryCounterEXT) {
        record_fake_ts(id);
        return;
    }
    es3_functions.glQueryCounterEXT(id, target);
}

void glGetQueryObjectiv(GLuint id, GLenum name, GLint * params) {
    context_t* ctx = emesis_get_context();
    if (!ctx || !params) return;
    if (!ctx->timer_query && lookup_fake_ts(id, NULL)) {
        *params = 1;
        return;
    }
    es3_functions.glGetQueryObjectuiv(id, name, (GLuint*)params);
}