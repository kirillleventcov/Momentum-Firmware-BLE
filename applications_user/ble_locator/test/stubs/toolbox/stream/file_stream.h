#pragma once
#include <toolbox/stream/stream.h>

static inline Stream* file_stream_alloc(Storage* storage) {
    UNUSED(storage);
    return calloc(1, sizeof(Stream));
}

static inline bool
    file_stream_open(Stream* s, const char* path, FS_AccessMode access, FS_OpenMode open_mode) {
    const char* mode = (access == FSAM_WRITE) ? "w" : "r";
    UNUSED(open_mode);
    s->fp = fopen(path, mode);
    return s->fp != NULL;
}
