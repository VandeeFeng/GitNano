#include "gitnano.h"
#include <errno.h>

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

int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

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

void get_git_timestamp(char *timestamp, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    strftime(timestamp, size, "%s %z", tm_info);
}

void get_object_path(const char *sha1, char *path) {
    snprintf(path, MAX_PATH, "%s/%.2s/%s", OBJECTS_DIR, sha1, sha1 + 2);
}

// Print commit hash with first 6 characters in orange
void print_colored_hash(const char *sha1) {
    if (!sha1 || strlen(sha1) < 6) {
        printf("%s", sha1 ? sha1 : "(null)");
        return;
    }

    // Orange color ANSI escape code
    printf("\x1b[38;5;208m%.6s\x1b[0m%s", sha1, sha1 + 6);
}