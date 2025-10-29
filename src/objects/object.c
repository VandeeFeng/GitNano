#include "gitnano.h"

// Helper function to create object header
static char* create_object_header(const char *type, size_t size, size_t *header_len_out) {
    *header_len_out = strlen(type) + 1 + 20 + 1; // type + space + size + null
    char *header = safe_malloc(*header_len_out);
    if (!header) return NULL;

    snprintf(header, *header_len_out, "%s %zu", type, size);
    return header;
}

// Helper function to combine header and data
static char* combine_header_data(const char *header, const void *data, size_t size, size_t *total_size_out) {
    size_t header_len = strlen(header);
    *total_size_out = header_len + 1 + size;

    char *combined = safe_malloc(*total_size_out);
    if (!combined) return NULL;

    memcpy(combined, header, header_len + 1);
    memcpy(combined + header_len + 1, data, size);
    return combined;
}

// Calculate hash of object data
int object_hash(const char *type, const void *data, size_t size, char *sha1_out) {
    size_t header_len, total_size;
    char *header = create_object_header(type, size, &header_len);
    if (!header) return -1;

    char *combined = combine_header_data(header, data, size, &total_size);
    if (!combined) {
        free(header);
        return -1;
    }

    int err = sha1_data(combined, total_size, sha1_out);

    free(header);
    free(combined);
    return err;
}

// Verify object integrity by reading it back and checking hash
static int verify_object_integrity(const char *sha1, const char *expected_type, const void *expected_data, size_t expected_size) {
    gitnano_object obj;
    int err = object_read(sha1, &obj);
    if (err != 0) {
        fprintf(stderr, "ERROR: verify_object_integrity: failed to read object %s for verification\n", sha1);
        return -1;
    }

    // Check object type
    if (strcmp(obj.type, expected_type) != 0) {
        fprintf(stderr, "ERROR: verify_object_integrity: type mismatch for object %s (expected %s, got %s)\n",
                sha1, expected_type, obj.type);
        object_free(&obj);
        return -1;
    }

    // Check object size
    if (obj.size != expected_size) {
        fprintf(stderr, "ERROR: verify_object_integrity: size mismatch for object %s (expected %zu, got %zu)\n",
                sha1, expected_size, obj.size);
        object_free(&obj);
        return -1;
    }

    // Check object data
    if (memcmp(obj.data, expected_data, expected_size) != 0) {
        fprintf(stderr, "ERROR: verify_object_integrity: data corruption detected for object %s\n", sha1);
        object_free(&obj);
        return -1;
    }

    object_free(&obj);
    return 0;
}

// Write object to object store
int object_write(const char *type, const void *data, size_t size, char *sha1_out) {
    char sha1[SHA1_HEX_SIZE];
    int err = object_hash(type, data, size, sha1);
    if (err != 0) {
        printf("ERROR: object_hash: %d\n", err);
        return err;
    }

    char path[MAX_PATH];
    get_object_path(sha1, path);

    if (file_exists(path)) {
        if (sha1_out) {
            strcpy(sha1_out, sha1);
        }
        return 0;
    }

    // Create object directory
    char dir_path[MAX_PATH];
    snprintf(dir_path, sizeof(dir_path), "%s/%.2s", OBJECTS_DIR, sha1);
    if ((err = mkdir_p(dir_path)) != 0) {
        printf("ERROR: mkdir_p: %d\n", err);
        return err;
    }

    // Create object content using helper functions
    size_t header_len, content_size;
    char *header = create_object_header(type, size, &header_len);
    if (!header) return -1;

    char *content = combine_header_data(header, data, size, &content_size);
    if (!content) {
        free(header);
        return -1;
    }

    // Compress content
    void *compressed = NULL;
    size_t compressed_size = 0;
    if ((err = compress_data(content, content_size, &compressed, &compressed_size)) != 0) {
        fprintf(stderr, "ERROR: compress_data: %d\n", err);
        free(header);
        free(content);
        return err;
    }

    // Write compressed data to file
    if ((err = write_file(path, compressed, compressed_size)) != 0) {
        fprintf(stderr, "ERROR: write_file: %d\n", err);
        free(header);
        free(content);
        free(compressed);
        return err;
    }

    // Verify object integrity immediately after writing
    if (verify_object_integrity(sha1, type, data, size) != 0) {
        fprintf(stderr, "ERROR: object_write: integrity check failed for object %s\n", sha1);
        // Remove corrupted object file
        unlink(path);
        free(header);
        free(content);
        free(compressed);
        return -1;
    }

    free(header);
    free(content);
    free(compressed);

    if (sha1_out) {
        strcpy(sha1_out, sha1);
    }

    return 0;
}

// Helper function to parse object header
static int parse_object_header(const char *header, char *type_out, size_t *size_out) {
    char *header_copy = safe_strdup(header);
    if (!header_copy) return -1;

    char *space_pos = strchr(header_copy, ' ');
    if (!space_pos) {
        free(header_copy);
        return -1;
    }

    *space_pos = '\0';
    strcpy(type_out, header_copy);
    *size_out = atol(space_pos + 1);

    free(header_copy);
    return 0;
}

// Read object from object store
int object_read(const char *sha1, gitnano_object *obj) {
    if (!sha1 || !obj) {
        fprintf(stderr, "ERROR: object_read: invalid arguments\n");
        return -1;
    }

    // Initialize object
    memset(obj, 0, sizeof(gitnano_object));

    char path[MAX_PATH];
    get_object_path(sha1, path);

    if (!file_exists(path)) {
        fprintf(stderr, "ERROR: object_read: object file not found at %s\n", path);
        return -1;
    }

    // Read compressed data
    size_t compressed_size;
    char *compressed = read_file(path, &compressed_size);
    if (!compressed) {
        fprintf(stderr, "ERROR: object_read: failed to read object file %s\n", path);
        return -1;
    }

    // Check if file is empty
    if (compressed_size == 0) {
        fprintf(stderr, "ERROR: object_read: object file %s is empty\n", path);
        free(compressed);
        return -1;
    }

    // Decompress data
    void *decompressed = NULL;
    size_t decompressed_size = 0;
    int err = decompress_data(compressed, compressed_size, &decompressed, &decompressed_size);
    if (err != 0) {
        fprintf(stderr, "ERROR: object_read: decompress_data failed with code %d (object %s may be corrupted)\n", err, sha1);
        free(compressed);
        return err;
    }

    free(compressed);

    // Check for minimum object size (type + space + size + null + at least 0 bytes data)
    if (decompressed_size < 4) {
        fprintf(stderr, "ERROR: object_read: decompressed data too small for valid object (%zu bytes)\n", decompressed_size);
        free(decompressed);
        return -1;
    }

    // Parse header: "type size\0"
    char *null_pos = memchr(decompressed, '\0', decompressed_size);
    if (!null_pos) {
        fprintf(stderr, "ERROR: object_read: could not find null terminator in object header (object %s may be corrupted)\n", sha1);
        free(decompressed);
        return -1;
    }

    size_t header_len = null_pos - (char*)decompressed;
    if (header_len == 0) {
        fprintf(stderr, "ERROR: object_read: empty object header (object %s may be corrupted)\n", sha1);
        free(decompressed);
        return -1;
    }

    char *header = safe_malloc(header_len + 1);
    if (!header) {
        free(decompressed);
        return -1;
    }

    memcpy(header, decompressed, header_len);
    header[header_len] = '\0';

    // Parse type and size using helper function
    err = parse_object_header(header, obj->type, &obj->size);
    if (err != 0) {
        fprintf(stderr, "ERROR: object_read: parse_object_header failed for object %s (header: '%s')\n", sha1, header);
        free(header);
        free(decompressed);
        return -1;
    }

    // Verify data size matches expected total size
    size_t expected_total_size = header_len + 1 + obj->size;
    if (decompressed_size != expected_total_size) {
        fprintf(stderr, "ERROR: object_read: size mismatch for object %s - decompressed: %zu, expected: %zu (header: %zu, data: %zu)\n",
                sha1, decompressed_size, expected_total_size, header_len + 1, obj->size);
        free(header);
        free(decompressed);
        return -1;
    }

    // Validate object type
    if (strlen(obj->type) == 0 || strlen(obj->type) >= sizeof(obj->type)) {
        fprintf(stderr, "ERROR: object_read: invalid object type '%s' for object %s\n", obj->type, sha1);
        free(header);
        free(decompressed);
        return -1;
    }

    // Allocate and copy data
    if (obj->size > 0) {
        obj->data = safe_malloc(obj->size);
        if (!obj->data) {
            fprintf(stderr, "ERROR: object_read: failed to allocate %zu bytes for object data\n", obj->size);
            free(header);
            free(decompressed);
            return -1;
        }

        memcpy(obj->data, (char*)decompressed + header_len + 1, obj->size);
    } else {
        obj->data = NULL;
    }

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
