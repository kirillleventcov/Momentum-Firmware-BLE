#pragma once
#include <furi.h>
#include <stdio.h>

#define RECORD_STORAGE "storage"
typedef struct Storage Storage;

typedef enum { FSAM_READ = 1, FSAM_WRITE = 2 } FS_AccessMode;
typedef enum { FSOM_OPEN_EXISTING = 1, FSOM_CREATE_ALWAYS = 2 } FS_OpenMode;

static inline void* furi_record_open(const char* name) {
    UNUSED(name);
    return (void*)1;
}
static inline void furi_record_close(const char* name) {
    UNUSED(name);
}

static inline bool storage_simply_remove(Storage* storage, const char* path) {
    UNUSED(storage);
    return remove(path) == 0;
}
