#include "gitnano.h"
#include <dirent.h>

int extract_blob(const char *sha1, const char *target_path) {
    int err;
    char *data = NULL;
    size_t size = 0;

    if ((err = blob_read(sha1, &data, &size)) != 0) {
        printf("ERROR: blob_read: %d\n", err);
        return err;
    }

    char *dir_path = safe_strdup(target_path);
    if (!dir_path) {
        free(data);
        return -1;
    }

    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        if ((err = mkdir_p(dir_path)) != 0) {
            printf("ERROR: mkdir_p: %d\n", err);
            free(dir_path);
            free(data);
            return err;
        }
    }
    free(dir_path);

    if ((err = write_file(target_path, data, size)) != 0) {
        printf("ERROR: write_file: %d\n", err);
        free(data);
        return err;
    }
    free(data);

    return 0;
}

int extract_tree_recursive(const char *tree_sha1, const char *base_path) {
    int err;
    tree_entry *entries = NULL;

    if ((err = tree_parse(tree_sha1, &entries)) != 0) {
        printf("ERROR: tree_parse: %d\n", err);
        return err;
    }

    tree_entry *current = entries;
    while (current) {
        char full_path[MAX_PATH];
        if (strlen(base_path) > 0) {
            if (strlen(base_path) + 1 + strlen(current->name) < MAX_PATH) {
                strcpy(full_path, base_path);
                strcat(full_path, "/");
                strcat(full_path, current->name);
            } else {
                printf("ERROR: Path too long\n");
                tree_free(entries);
                return -1;
            }
        } else {
            strcpy(full_path, current->name);
        }

        if (strcmp(current->type, "blob") == 0) {
            if ((err = extract_blob(current->sha1, full_path)) != 0) {
                printf("ERROR: extract_blob: %d\n", err);
                tree_free(entries);
                return err;
            }
        } else if (strcmp(current->type, "tree") == 0) {
            if ((err = mkdir_p(full_path)) != 0) {
                printf("ERROR: mkdir_p: %d\n", err);
                tree_free(entries);
                return err;
            }
            if ((err = extract_tree_recursive(current->sha1, full_path)) != 0) {
                tree_free(entries);
                return err;
            }
        }

        current = current->next;
    }

    tree_free(entries);
    return 0;
}

int collect_working_files(const char *dir_path, file_entry **files) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".gitnano") == 0) {
            continue;
        }

        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            collect_working_files(full_path, files);
        } else {
            file_entry *file = safe_malloc(sizeof(file_entry));
            if (!file) {
                closedir(dir);
                return -1;
            }

            const char *base = (strcmp(dir_path, ".") == 0) ? "" : dir_path + 2;
            if (strlen(base) > 0) {
                file->path = safe_asprintf("%s/%s", base, entry->d_name);
            } else {
                file->path = safe_strdup(entry->d_name);
            }

            file->next = *files;
            *files = file;
        }
    }

    closedir(dir);
    return 0;
}

int file_in_target_tree(const char *path, file_entry *target_files) {
    while (target_files) {
        if (strcmp(target_files->path, path) == 0) {
            return 1;
        }
        target_files = target_files->next;
    }
    return 0;
}

void free_file_list(file_entry *list) {
    while (list) {
        file_entry *next = list->next;
        free(list->path);
        free(list);
        list = next;
    }
}

int cleanup_extra_files(const char *base_path, file_entry *target_files) {
    file_entry *working_files = NULL;

    if (collect_working_files(".", &working_files) != 0) {
        free_file_list(working_files);
        return -1;
    }

    file_entry *current = working_files;
    while (current) {
        if (!file_in_target_tree(current->path, target_files)) {
            char *full_path = safe_asprintf("%s/%s", base_path, current->path);

            if (unlink(full_path) != 0) {
                printf("Warning: Could not delete file %s\n", full_path);
            }
            free(full_path);
        }
        current = current->next;
    }

    free_file_list(working_files);
    return 0;
}

int collect_target_files(const char *tree_sha1, const char *base_path, file_entry **files) {
    tree_entry *entries = NULL;
    if (tree_parse(tree_sha1, &entries) != 0) {
        return -1;
    }

    tree_entry *current = entries;
    while (current) {
        file_entry *file = safe_malloc(sizeof(file_entry));
        if (!file) {
            tree_free(entries);
            return -1;
        }

        if (strlen(base_path) > 0) {
            file->path = safe_asprintf("%s/%s", base_path, current->name);
        } else {
            file->path = safe_strdup(current->name);
        }

        if (strcmp(current->type, "tree") == 0) {
            collect_target_files(current->sha1, file->path, files);
            free(file);
        } else {
            file->next = *files;
            *files = file;
        }

        current = current->next;
    }

    tree_free(entries);
    return 0;
}