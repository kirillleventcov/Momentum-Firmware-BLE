#pragma once
#include <furi.h>
#include <storage/storage.h>

typedef struct {
    FILE* fp;
} Stream;

static inline void stream_free(Stream* s) {
    if(!s) return;
    if(s->fp) fclose(s->fp);
    free(s);
}

static inline size_t stream_write_string(Stream* s, FuriString* str) {
    const char* text = furi_string_get_cstr(str);
    return fwrite(text, 1, strlen(text), s->fp);
}

static inline bool stream_read_line(Stream* s, FuriString* out) {
    char buf[512];
    if(!fgets(buf, sizeof(buf), s->fp)) return false;
    furi_string_reset(out);
    furi_string_cat_str(out, buf);
    return true;
}
