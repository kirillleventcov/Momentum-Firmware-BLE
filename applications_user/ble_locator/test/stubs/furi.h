/* Minimal host stubs so the Device Locator logic can be unit tested off-device */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))
#define UNUSED(x)   ((void)(x))

#define FURI_LOG_E(tag, ...) ((void)0)
#define FURI_LOG_W(tag, ...) ((void)0)
#define FURI_LOG_I(tag, ...) ((void)0)
#define FURI_LOG_D(tag, ...) ((void)0)

#define furi_check(x)  assert(x)
#define furi_assert(x) assert(x)

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} FuriString;

static inline void furi_string_ensure(FuriString* s, size_t need) {
    if(s->cap >= need + 1) return;
    size_t cap = s->cap ? s->cap : 64;
    while(cap < need + 1) cap *= 2;
    s->data = realloc(s->data, cap);
    s->cap = cap;
}

static inline FuriString* furi_string_alloc(void) {
    FuriString* s = calloc(1, sizeof(FuriString));
    furi_string_ensure(s, 0);
    s->data[0] = '\0';
    return s;
}

static inline FuriString* furi_string_alloc_set_str(const char* str) {
    FuriString* s = furi_string_alloc();
    furi_string_ensure(s, strlen(str));
    strcpy(s->data, str);
    s->len = strlen(str);
    return s;
}

static inline void furi_string_free(FuriString* s) {
    if(!s) return;
    free(s->data);
    free(s);
}

static inline void furi_string_reset(FuriString* s) {
    s->len = 0;
    if(s->data) s->data[0] = '\0';
}

static inline const char* furi_string_get_cstr(FuriString* s) {
    return s->data ? s->data : "";
}

static inline void furi_string_cat_str(FuriString* s, const char* str) {
    furi_string_ensure(s, s->len + strlen(str));
    memcpy(s->data + s->len, str, strlen(str) + 1);
    s->len += strlen(str);
}

static inline void furi_string_vcat_printf(FuriString* s, const char* fmt, va_list ap) {
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    furi_string_cat_str(s, buf);
}

static inline void furi_string_cat_printf(FuriString* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    furi_string_vcat_printf(s, fmt, ap);
    va_end(ap);
}

static inline void furi_string_printf(FuriString* s, const char* fmt, ...) {
    furi_string_reset(s);
    va_list ap;
    va_start(ap, fmt);
    furi_string_vcat_printf(s, fmt, ap);
    va_end(ap);
}

static inline void furi_string_trim(FuriString* s) {
    if(!s->data) return;
    size_t start = 0;
    while(s->data[start] == ' ' || s->data[start] == '\t' || s->data[start] == '\r' ||
          s->data[start] == '\n')
        start++;
    size_t end = strlen(s->data);
    while(end > start && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t' ||
                          s->data[end - 1] == '\r' || s->data[end - 1] == '\n'))
        end--;
    memmove(s->data, s->data + start, end - start);
    s->data[end - start] = '\0';
    s->len = end - start;
}
