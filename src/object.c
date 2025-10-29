#include "gitnano.h"

// Calculate hash of object data
int object_hash(const char *type, const void *data, size_t size, char *sha1_out) {
    // Create header: type + ' ' + size + '\0'
    size_t header_len = strlen(type) + 1 + 20 + 1; // type + space + size + null
    char *header = malloc(header_len);
    if (!header) return -1;

    snprintf(header, header_len, "%s %zu", type, size);

    // Combine header and data
    size_t total_size = strlen(header) + 1 + size;
    char *combined = malloc(total_size);
    if (!combined) {
        free(header);
        return -1;
    }

    memcpy(combined, header, strlen(header) + 1);
    memcpy(combined + strlen(header) + 1, data, size);

    // Calculate SHA-1
    int result = sha1_data(combined, total_size, sha1_out);

    free(header);
    free(combined);
    return result;
}

// Write object to object store
int object_write(const char *type, const void *data, size_t size, char *sha1_out) {
    char sha1[SHA1_HEX_SIZE];

    // Calculate object hash
    if (object_hash(type, data, size, sha1) != 0) {
        return -1;
    }

    // Check if object already exists
    char path[MAX_PATH];
    get_object_path(sha1, path);

    if (file_exists(path)) {
        // Object already exists, just return the hash
        if (sha1_out) {
            strcpy(sha1_out, sha1);
        }
        return 0;
    }

    // Create object directory
    char dir_path[MAX_PATH];
    snprintf(dir_path, sizeof(dir_path), "%s/%.2s", OBJECTS_DIR, sha1);
    if (mkdir_p(dir_path) != 0) {
        return -1;
    }

    // Create object content: header + '\0' + data
    size_t header_len = strlen(type) + 1 + 20 + 1;
    char *header = malloc(header_len);
    if (!header) return -1;

    snprintf(header, header_len, "%s %zu", type, size);

    size_t content_size = strlen(header) + 1 + size;
    char *content = malloc(content_size);
    if (!content) {
        free(header);
        return -1;
    }

    memcpy(content, header, strlen(header) + 1);
    memcpy(content + strlen(header) + 1, data, size);

    // Compress content
    void *compressed = NULL;
    size_t compressed_size = 0;
    if (compress_data(content, content_size, &compressed, &compressed_size) != 0) {
        free(header);
        free(content);
        return -1;
    }

    // Write compressed data to file
    int result = write_file(path, compressed, compressed_size);

    free(header);
    free(content);
    free(compressed);

    if (result == 0 && sha1_out) {
        strcpy(sha1_out, sha1);
    }

    return result;
}

// Read object from object store
int object_read(const char *sha1, gitnano_object *obj) {
    char path[MAX_PATH];
    get_object_path(sha1, path);

    if (!file_exists(path)) {
        return -1;
    }

    // Read compressed data
    size_t compressed_size;
    char *compressed = read_file(path, &compressed_size);
    if (!compressed) {
        return -1;
    }

    // Decompress data
    void *decompressed = NULL;
    size_t decompressed_size = 0;
    if (decompress_data(compressed, compressed_size, &decompressed, &decompressed_size) != 0) {
        free(compressed);
        return -1;
    }

    free(compressed);

    // Parse header: "type size\0"
    char *null_pos = memchr(decompressed, '\0', decompressed_size);
    if (!null_pos) {
        free(decompressed);
        return -1;
    }

    size_t header_len = null_pos - (char*)decompressed;
    char *header = malloc(header_len + 1);
    if (!header) {
        free(decompressed);
        return -1;
    }

    memcpy(header, decompressed, header_len);
    header[header_len] = '\0';

    // Parse type and size from header
    char *space_pos = strchr(header, ' ');
    if (!space_pos) {
        free(header);
        free(decompressed);
        return -1;
    }

    *space_pos = '\0';
    strcpy(obj->type, header);
    obj->size = atol(space_pos + 1);

    // Copy data
    obj->data = malloc(obj->size);
    if (!obj->data) {
        free(header);
        free(decompressed);
        return -1;
    }

    memcpy(obj->data, (char*)decompressed + header_len + 1, obj->size);

    free(header);
    free(decompressed);

    return 0;
}

// Free object memory
void object_free(gitnano_object *obj) {
    if (obj) {
        free(obj->data);
        obj->data = NULL;
        obj->size = 0;
    }
}