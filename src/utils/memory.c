#define _GNU_SOURCE
#include "gitnano.h"

void *safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "ERROR: malloc failed for size %zu\n", size);
        exit(1);
    }
    return ptr;
}

void *safe_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "ERROR: realloc failed for size %zu\n", size);
        exit(1);
    }
    return new_ptr;
}

char *safe_strdup(const char *s) {
    if (!s) return NULL;
    char *dup = malloc(strlen(s) + 1);
    if (!dup) {
        fprintf(stderr, "ERROR: strdup failed\n");
        exit(1);
    }
    strcpy(dup, s);
    return dup;
}

char *safe_asprintf(const char *fmt, ...) {
    va_list args;
    char *ptr;
    va_start(args, fmt);
    if (vasprintf(&ptr, fmt, args) == -1) {
        fprintf(stderr, "ERROR: vasprintf failed\n");
        exit(1);
    }
    va_end(args);
    return ptr;
}
