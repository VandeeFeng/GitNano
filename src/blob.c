#include "gitnano.h"

// Write blob object
int blob_write(const char *data, size_t size, char *sha1_out) {
    return object_write("blob", data, size, sha1_out);
}

// Read blob object
int blob_read(const char *sha1, char **data, size_t *size) {
    gitnano_object obj;

    if (object_read(sha1, &obj) != 0) {
        return -1;
    }

    if (strcmp(obj.type, "blob") != 0) {
        object_free(&obj);
        return -1;
    }

    *data = malloc(obj.size + 1);
    if (!*data) {
        object_free(&obj);
        return -1;
    }

    memcpy(*data, obj.data, obj.size);
    (*data)[obj.size] = '\0';
    *size = obj.size;

    object_free(&obj);
    return 0;
}

// Create blob from file
int blob_create_from_file(const char *filepath, char *sha1_out) {
    size_t size;
    char *data = read_file(filepath, &size);
    if (!data) {
        return -1;
    }

    int result = blob_write(data, size, sha1_out);
    free(data);
    return result;
}

// Check if blob exists
int blob_exists(const char *sha1) {
    char path[MAX_PATH];
    get_object_path(sha1, path);
    return file_exists(path);
}

// Get blob content size
int blob_size(const char *sha1, size_t *size_out) {
    gitnano_object obj;

    if (object_read(sha1, &obj) != 0) {
        return -1;
    }

    if (strcmp(obj.type, "blob") != 0) {
        object_free(&obj);
        return -1;
    }

    *size_out = obj.size;
    object_free(&obj);
    return 0;
}

// Stream blob content to file
int blob_cat(const char *sha1, FILE *out) {
    gitnano_object obj;

    if (object_read(sha1, &obj) != 0) {
        return -1;
    }

    if (strcmp(obj.type, "blob") != 0) {
        object_free(&obj);
        return -1;
    }

    size_t written = fwrite(obj.data, 1, obj.size, out);
    object_free(&obj);

    return (written == obj.size) ? 0 : -1;
}