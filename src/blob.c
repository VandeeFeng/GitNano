#include "gitnano.h"

int blob_write(const char *data, size_t size, char *sha1_out) {
    return object_write("blob", data, size, sha1_out);
}

int blob_read(const char *sha1, char **data, size_t *size) {
    int err;
    gitnano_object obj;

    if ((err = object_read(sha1, &obj)) != 0) {
        printf("ERROR: object_read: %d\n", err);
        return err;
    }

    if (strcmp(obj.type, "blob") != 0) {
        object_free(&obj);
        printf("ERROR: object type is not blob\n");
        return -1;
    }

    *data = malloc(obj.size + 1);
    if (!*data) {
        printf("ERROR: malloc data: %d\n", -1);
        object_free(&obj);
        return -1;
    }

    memcpy(*data, obj.data, obj.size);
    (*data)[obj.size] = '\0';
    *size = obj.size;

    object_free(&obj);
    return 0;
}

int blob_create_from_file(const char *filepath, char *sha1_out) {
    int err;
    size_t size;
    char *data = read_file(filepath, &size);
    if (!data) {
        printf("ERROR: read_file: %d\n", -1);
        return -1;
    }

    if ((err = blob_write(data, size, sha1_out)) != 0) {
        printf("ERROR: blob_write: %d\n", err);
        free(data);
        return err;
    }
    free(data);
    return 0;
}

int blob_exists(const char *sha1) {
    char path[MAX_PATH];
    get_object_path(sha1, path);
    return file_exists(path);
}

// Get blob content size
int blob_size(const char *sha1, size_t *size_out) {
    int err;
    gitnano_object obj;

    if ((err = object_read(sha1, &obj)) != 0) {
        printf("ERROR: object_read: %d\n", err);
        return err;
    }

    if (strcmp(obj.type, "blob") != 0) {
        object_free(&obj);
        printf("ERROR: object type is not blob\n");
        return -1;
    }

    *size_out = obj.size;
    object_free(&obj);
    return 0;
}

// Stream blob content to file
int blob_cat(const char *sha1, FILE *out) {
    int err;
    gitnano_object obj;

    if ((err = object_read(sha1, &obj)) != 0) {
        printf("ERROR: object_read: %d\n", err);
        return err;
    }

    if (strcmp(obj.type, "blob") != 0) {
        object_free(&obj);
        printf("ERROR: object type is not blob\n");
        return -1;
    }

    size_t written = fwrite(obj.data, 1, obj.size, out);
    object_free(&obj);

    if (written != obj.size) {
        printf("ERROR: fwrite incomplete\n");
        return -1;
    }

    return 0;
}
