//
// Created by whbex on 25.11.2025.
//

#include <GL/gl.h>
#include <GL/glext.h>
#include <stdbool.h>
#include "egl.h"
#include "proc.h"

#define FAKE_TS_RING 32
static thread_local GLuint s_fake_ts_ids[FAKE_TS_RING];
static thread_local unsigned int s_fake_ts_cursor = 0;

static void record_fake_ts(GLuint id) {
    if (id == 0) return;
    for (unsigned int i = 0; i < FAKE_TS_RING; i++) {
        if (s_fake_ts_ids[i] == id) return;
    }
    s_fake_ts_ids[s_fake_ts_cursor & (FAKE_TS_RING - 1)] = id;
    s_fake_ts_cursor++;
}

static bool is_fake_ts(GLuint id) {
    if (id == 0) return false;
    for (unsigned int i = 0; i < FAKE_TS_RING; i++) {
        if (s_fake_ts_ids[i] == id) return true;
    }
    return false;
}

void glGetQueryObjecti64v(GLuint id, GLenum pname, int64_t* params){
    emesis_ensure_current_context();
    if (!current_context || !params) return;
    if(!current_context->timer_query || !es3_functions.glGetQueryObjecti64vEXT){
        *params = 1;
        return;
    }
    es3_functions.glGetQueryObjecti64vEXT(id, pname, params);
}

void glGetQueryObjectui64v(GLuint id, GLenum pname, uint64_t* params){
    emesis_ensure_current_context();
    if (!current_context || !params) return;
    if(!current_context->timer_query || !es3_functions.glGetQueryObjectui64vEXT){
        *params = 1;
        return;
    }
    es3_functions.glGetQueryObjectui64vEXT(id, pname, params);
}

void glQueryCounter(GLuint id, GLenum target){
    emesis_ensure_current_context();
    if(!current_context) return;
    if(!current_context->timer_query || !es3_functions.glQueryCounterEXT) {
        record_fake_ts(id);
        return;
    }
    es3_functions.glQueryCounterEXT(id, target);
}

void glGetQueryObjectiv(GLuint id, GLenum name, GLint * params) {
    emesis_ensure_current_context();
    if (!current_context || !params) return;
    if (!current_context->timer_query && is_fake_ts(id)) {
        *params = 1;
        return;
    }
    es3_functions.glGetQueryObjectuiv(id, name, (GLuint*)params);
}