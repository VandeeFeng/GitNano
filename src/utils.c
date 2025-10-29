#include "gitnano.h"
#include <openssl/evp.h>
#include <zlib.h>
#include <errno.h>

// Compute SHA-1 hash of a file
int sha1_file(const char *path, char *sha1_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        fclose(fp);
        return -1;
    }

    if (EVP_DigestInit_ex(md_ctx, EVP_sha1(), NULL) != 1) {
        EVP_MD_CTX_free(md_ctx);
        fclose(fp);
        return -1;
    }

    unsigned char buffer[8192];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (EVP_DigestUpdate(md_ctx, buffer, bytes_read) != 1) {
            EVP_MD_CTX_free(md_ctx);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    if (EVP_DigestFinal_ex(md_ctx, digest, &digest_len) != 1) {
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
    if (!md_ctx) return -1;

    if (EVP_DigestInit_ex(md_ctx, EVP_sha1(), NULL) != 1) {
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    if (EVP_DigestUpdate(md_ctx, data, size) != 1) {
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    if (EVP_DigestFinal_ex(md_ctx, digest, &digest_len) != 1) {
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
    uLongf compressed_size = compressBound(input_size);
    *output = malloc(compressed_size);
    if (!*output) return -1;

    int result = compress2(*output, &compressed_size, input, input_size, 9);
    if (result != Z_OK) {
        free(*output);
        return -1;
    }

    *output_size = compressed_size;
    return 0;
}

// Decompress data using zlib
int decompress_data(const void *input, size_t input_size,
                    void **output, size_t *output_size) {
    // Start with a reasonable estimate
    uLongf decompressed_size = input_size * 4;
    *output = malloc(decompressed_size);
    if (!*output) return -1;

    int result;
    while (1) {
        result = uncompress(*output, &decompressed_size, input, input_size);

        if (result == Z_OK) {
            *output_size = decompressed_size;
            return 0;
        }

        if (result == Z_BUF_ERROR) {
            // Need more space
            free(*output);
            decompressed_size *= 2;
            *output = malloc(decompressed_size);
            if (!*output) return -1;
            continue;
        }

        // Other error
        free(*output);
        return -1;
    }
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
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
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
    if (!fp) return -1;

    size_t bytes_written = fwrite(data, 1, size, fp);
    fclose(fp);

    return (bytes_written == size) ? 0 : -1;
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