#include "gitnano.h"
#include <openssl/evp.h>
#include <zlib.h>
#include <errno.h>

// Compute SHA-1 hash of a file
int sha1_file(const char *path, char *sha1_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("ERROR: fopen: %d\n", -1);
        return -1;
    }

    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        printf("ERROR: EVP_MD_CTX_new: %d\n", -1);
        fclose(fp);
        return -1;
    }

    if (EVP_DigestInit_ex(md_ctx, EVP_sha1(), NULL) != 1) {
        printf("ERROR: EVP_DigestInit_ex: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        fclose(fp);
        return -1;
    }

    unsigned char buffer[8192];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (EVP_DigestUpdate(md_ctx, buffer, bytes_read) != 1) {
            printf("ERROR: EVP_DigestUpdate: %d\n", -1);
            EVP_MD_CTX_free(md_ctx);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    if (EVP_DigestFinal_ex(md_ctx, digest, &digest_len) != 1) {
        printf("ERROR: EVP_DigestFinal_ex: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    EVP_MD_CTX_free(md_ctx);

    for (unsigned int i = 0; i < digest_len; i++) {
        sprintf(sha1_out + (i * 2), "%02x", digest[i]);
    }
    sha1_out[SHA1_HEX_SIZE - 1] = '\0';

    return 0;
}

// Compute SHA-1 hash of data in memory
int sha1_data(const void *data, size_t size, char *sha1_out) {
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        printf("ERROR: EVP_MD_CTX_new: %d\n", -1);
        return -1;
    }

    if (EVP_DigestInit_ex(md_ctx, EVP_sha1(), NULL) != 1) {
        printf("ERROR: EVP_DigestInit_ex: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    if (EVP_DigestUpdate(md_ctx, data, size) != 1) {
        printf("ERROR: EVP_DigestUpdate: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    if (EVP_DigestFinal_ex(md_ctx, digest, &digest_len) != 1) {
        printf("ERROR: EVP_DigestFinal_ex: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    EVP_MD_CTX_free(md_ctx);

    for (unsigned int i = 0; i < digest_len; i++) {
        sprintf(sha1_out + (i * 2), "%02x", digest[i]);
    }
    sha1_out[SHA1_HEX_SIZE - 1] = '\0';

    return 0;
}

// Compress data using zlib
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

    // Shrink buffer to actual compressed size if needed
    if (compressed_size < compressBound(input_size)) {
        void *shrunk_output = safe_realloc(*output, compressed_size);
        if (shrunk_output) {
            *output = shrunk_output;
        }
        // If realloc fails, keep the original buffer - not a critical error
    }

    *output_size = compressed_size;
    return 0;
}

// Decompress data using zlib
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

    // Check for obviously corrupted input
    if (input_size < 8) {  // Minimum valid zlib stream size
        fprintf(stderr, "ERROR: decompress_data: input too small to be valid compressed data (%zu bytes)\n", input_size);
        return -1;
    }

    // Start with a reasonable buffer size - compressed data can expand significantly
    uLongf buffer_size = input_size * 4;
    if (buffer_size < 1024) buffer_size = 1024;  // Minimum buffer size
    if (buffer_size > 50 * 1024 * 1024) buffer_size = 50 * 1024 * 1024;  // Cap at 50MB

    *output = safe_malloc(buffer_size);
    if (!*output) {
        fprintf(stderr, "ERROR: decompress_data: failed to allocate initial buffer (%zu bytes)\n", buffer_size);
        return -1;
    }

    int attempts = 0;
    const int max_attempts = 10;  // Prevent infinite loops

    while (attempts < max_attempts) {
        uLongf dest_len = buffer_size;
        int result = uncompress(*output, &dest_len, input, input_size);

        if (result == Z_OK) {
            *output_size = dest_len;
            // Shrink buffer to actual size if it's significantly larger
            if (*output_size < buffer_size && *output_size > 0) {
                void *shrunk_output = safe_realloc(*output, *output_size);
                if (shrunk_output) {
                    *output = shrunk_output;
                }
                // If realloc fails, keep the original buffer - not critical
            }
            return 0;
        }

        if (result == Z_BUF_ERROR) {
            attempts++;
            buffer_size *= 2;
            // Prevent unreasonable buffer growth
            if (buffer_size > 100 * 1024 * 1024) {  // 100MB limit
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

// Create directory recursively (like mkdir -p)
int mkdir_p(const char *path) {
    char tmp[MAX_PATH];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                printf("ERROR: mkdir: %d\n", -1);
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        printf("ERROR: mkdir: %d\n", -1);
        return -1;
    }

    return 0;
}

// Check if file exists
int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

// Read entire file into memory
char *read_file(const char *path, size_t *size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    *size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *data = malloc(*size + 1);
    if (!data) {
        fclose(fp);
        return NULL;
    }

    size_t bytes_read = fread(data, 1, *size, fp);
    fclose(fp);

    if (bytes_read != *size) {
        free(data);
        return NULL;
    }

    data[*size] = '\0';
    return data;
}

// Write data to file
int write_file(const char *path, const void *data, size_t size) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "ERROR: fopen failed for path '%s': %s\n", path, strerror(errno));
        return -1;
    }

    size_t bytes_written = fwrite(data, 1, size, fp);
    fclose(fp);

    if (bytes_written != size) {
        fprintf(stderr, "ERROR: fwrite incomplete for path '%s'. Wrote %zu of %zu bytes.\n",
                path, bytes_written, size);
        return -1;
    }

    return 0;
}

// Get current timestamp in git format
void get_git_timestamp(char *timestamp, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    strftime(timestamp, size, "%s %z", tm_info);
}

// Create object path from SHA-1
void get_object_path(const char *sha1, char *path) {
    snprintf(path, MAX_PATH, "%s/%.2s/%s", OBJECTS_DIR, sha1, sha1 + 2);
}

// Check if there's a potential Git/GitNano path conflict
int check_git_nano_conflict(void) {
    if (file_exists(".git") && file_exists(".gitnano")) {
        fprintf(stderr, "WARNING: Both .git and .gitnano repositories detected in this directory.\n");
        fprintf(stderr, "GitNano will only use objects from .gitnano repository.\n");
        return 1; // Conflict detected
    }
    return 0; // No conflict
}

// Validate path is within GitNano directory (not Git)
int is_gitnano_path(const char *path) {
    if (!path) return 0;

    // Check if path contains .git but not .gitnano
    if (strstr(path, "/.git/") && !strstr(path, ".gitnano")) {
        return 0; // This is a Git path, not GitNano
    }

    return 1; // Safe path
}

// Safe memory allocation helper functions
void *safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "ERROR: malloc failed for size %zu\n", size);
        return NULL;
    }
    return ptr;
}

void *safe_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "ERROR: realloc failed for size %zu\n", size);
        return NULL;
    }
    return new_ptr;
}

char *safe_strdup(const char *s) {
    if (!s) return NULL;
    char *dup = malloc(strlen(s) + 1);
    if (!dup) {
        fprintf(stderr, "ERROR: strdup failed\n");
        return NULL;
    }
    strcpy(dup, s);
    return dup;
}
