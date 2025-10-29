#include "gitnano.h"
#include <zlib.h>
#include <errno.h>

int compress_data(const void *input, size_t input_size,
                  void **output, size_t *output_size) {
    if (!input || !output || !output_size) {
        fprintf(stderr, "ERROR: compress_data: invalid arguments\n");
        return -1;
    }

    if (input_size == 0) {
        *output = NULL;
        *output_size = 0;
        return 0;
    }

    uLongf compressed_size = compressBound(input_size);
    *output = safe_malloc(compressed_size);
    if (!*output) {
        fprintf(stderr, "ERROR: compress_data: failed to allocate compression buffer\n");
        return -1;
    }

    int result = compress2(*output, &compressed_size, input, input_size, 9);
    if (result != Z_OK) {
        fprintf(stderr, "ERROR: compress_data: compression failed with code %d\n", result);
        free(*output);
        *output = NULL;
        return -1;
    }

    if (compressed_size < compressBound(input_size)) {
        void *shrunk_output = safe_realloc(*output, compressed_size);
        if (shrunk_output) {
            *output = shrunk_output;
        }
    }

    *output_size = compressed_size;
    return 0;
}

int decompress_data(const void *input, size_t input_size,
                    void **output, size_t *output_size) {
    if (!input || !output || !output_size) {
        fprintf(stderr, "ERROR: decompress_data: invalid arguments\n");
        return -1;
    }

    if (input_size == 0) {
        *output = NULL;
        *output_size = 0;
        return 0;
    }

    if (input_size < 8) {
        fprintf(stderr, "ERROR: decompress_data: input too small to be valid compressed data (%zu bytes)\n", input_size);
        return -1;
    }

    uLongf buffer_size = input_size * 4;
    if (buffer_size < 1024) buffer_size = 1024;
    if (buffer_size > 50 * 1024 * 1024) buffer_size = 50 * 1024 * 1024;

    *output = safe_malloc(buffer_size);
    if (!*output) {
        fprintf(stderr, "ERROR: decompress_data: failed to allocate initial buffer (%zu bytes)\n", buffer_size);
        return -1;
    }

    int attempts = 0;
    const int max_attempts = 10;

    while (attempts < max_attempts) {
        uLongf dest_len = buffer_size;
        int result = uncompress(*output, &dest_len, input, input_size);

        if (result == Z_OK) {
            *output_size = dest_len;
            if (*output_size < buffer_size && *output_size > 0) {
                void *shrunk_output = safe_realloc(*output, *output_size);
                if (shrunk_output) {
                    *output = shrunk_output;
                }
            }
            return 0;
        }

        if (result == Z_BUF_ERROR) {
            attempts++;
            buffer_size *= 2;
            if (buffer_size > 100 * 1024 * 1024) {
                fprintf(stderr, "ERROR: decompress_data: decompression buffer too large (%zu bytes), data may be corrupted\n", buffer_size);
                free(*output);
                *output = NULL;
                return -1;
            }

            void *new_output = safe_realloc(*output, buffer_size);
            if (!new_output) {
                fprintf(stderr, "ERROR: decompress_data: failed to reallocate buffer to %zu bytes\n", buffer_size);
                free(*output);
                *output = NULL;
                return -1;
            }
            *output = new_output;
        } else {
            fprintf(stderr, "ERROR: decompress_data: uncompress failed with code %d", result);
            switch (result) {
                case Z_MEM_ERROR:
                    fprintf(stderr, " (memory error)\n");
                    break;
                case Z_DATA_ERROR:
                    fprintf(stderr, " (data corrupted or incomplete)\n");
                    break;
                default:
                    fprintf(stderr, " (unknown error)\n");
                    break;
            }
            free(*output);
            *output = NULL;
            return -1;
        }
    }

    fprintf(stderr, "ERROR: decompress_data: maximum expansion attempts exceeded, data may be corrupted\n");
    free(*output);
    *output = NULL;
    return -1;
}