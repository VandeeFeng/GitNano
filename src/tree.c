#define _GNU_SOURCE
#include "gitnano.h"
#include <dirent.h>

static int tree_serialize(tree_entry *entries, char **data_out, size_t *size_out);
static int extract_blob(const char *sha1, const char *target_path);
static int extract_tree_recursive(const char *tree_sha1, const char *base_path);

// Create a new tree entry
tree_entry *tree_entry_new(const char *mode, const char *type,
                          const char *sha1, const char *name) {
    tree_entry *entry = malloc(sizeof(tree_entry));
    if (!entry) return NULL;

    strncpy(entry->mode, mode, sizeof(entry->mode) - 1);
    strncpy(entry->type, type, sizeof(entry->type) - 1);
    strncpy(entry->sha1, sha1, sizeof(entry->sha1) - 1);
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->next = NULL;

    return entry;
}

// Free tree entries list
void tree_free(tree_entry *entries) {
    while (entries) {
        tree_entry *next = entries->next;
        free(entries);
        entries = next;
    }
}

// Add entry to tree list (in sorted order)
void tree_entry_add(tree_entry **entries, tree_entry *new_entry) {
    if (!*entries) {
        *entries = new_entry;
        return;
    }

    tree_entry **current = entries;
    while (*current && strcmp((*current)->name, new_entry->name) < 0) {
        current = &(*current)->next;
    }

    new_entry->next = *current;
    *current = new_entry;
}

// Build tree from directory
int tree_build(const char *path, char *sha1_out) {
    int err;
    DIR *dir = opendir(path);
    if (!dir) {
        printf("ERROR: opendir: %d\n", -1);
        return -1;
    }

    tree_entry *entries = NULL;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. and .gitnano
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".gitnano") == 0) {
            continue;
        }

        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            printf("ERROR: stat: %d\n", -1);
            tree_free(entries);
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            // Build subtree
            char subtree_sha1[SHA1_HEX_SIZE];
            if ((err = tree_build(full_path, subtree_sha1)) != 0) {
                printf("ERROR: tree_build: %d\n", err);
                tree_free(entries);
                closedir(dir);
                return err;
            }

            tree_entry *dir_entry = tree_entry_new("040000", "tree",
                                                  subtree_sha1, entry->d_name);
            if (!dir_entry) {
                printf("ERROR: tree_entry_new: %d\n", -1);
                tree_free(entries);
                closedir(dir);
                return -1;
            }

            tree_entry_add(&entries, dir_entry);
        } else {
            // Create blob for file
            char blob_sha1[SHA1_HEX_SIZE];
            if ((err = blob_create_from_file(full_path, blob_sha1)) != 0) {
                printf("ERROR: blob_create_from_file: %d\n", err);
                tree_free(entries);
                closedir(dir);
                return err;
            }

            // Determine file mode
            char mode[8];
            if (st.st_mode & S_IXUSR) {
                strcpy(mode, "100755"); // executable
            } else {
                strcpy(mode, "100644"); // regular file
            }

            tree_entry *file_entry = tree_entry_new(mode, "blob",
                                                   blob_sha1, entry->d_name);
            if (!file_entry) {
                printf("ERROR: tree_entry_new: %d\n", -1);
                tree_free(entries);
                closedir(dir);
                return -1;
            }

            tree_entry_add(&entries, file_entry);
        }
    }

    closedir(dir);

    // Build tree data using the new serialize function
    char *tree_data;
    size_t tree_size;
    if ((err = tree_serialize(entries, &tree_data, &tree_size)) != 0) {
        printf("ERROR: tree_serialize: %d\n", err);
        tree_free(entries);
        return err;
    }

    // Write tree object
    if ((err = object_write("tree", tree_data, tree_size, sha1_out)) != 0) {
        printf("ERROR: object_write: %d\n", err);
        free(tree_data);
        tree_free(entries);
        return err;
    }

    free(tree_data);
    tree_free(entries);

    return 0;
}

// Parse tree object
int tree_parse(const char *sha1, tree_entry **entries) {
    int err;
    gitnano_object obj;

    if ((err = object_read(sha1, &obj)) != 0) {
        printf("ERROR: object_read: %d\n", err);
        return err;
    }

    if (strcmp(obj.type, "tree") != 0) {
        object_free(&obj);
        printf("ERROR: object type is not tree\n");
        return -1;
    }

    char *ptr = obj.data;
    char *end = ptr + obj.size;
    *entries = NULL;

    while (ptr < end) {
        // Parse mode
        char *space = memchr(ptr, ' ', end - ptr);
        if (!space) break;

        *space = '\0';
        char mode[8];
        strncpy(mode, ptr, sizeof(mode) - 1);
        mode[sizeof(mode) - 1] = '\0';

        ptr = space + 1;

        // Parse name
        char *null_pos = memchr(ptr, '\0', end - ptr);
        if (!null_pos) break;

        char *name = ptr;
        ptr = null_pos + 1;

        if (ptr + 20 > end) break;

        // Parse SHA1 (convert binary to hex)
        char sha1_hex[SHA1_HEX_SIZE];
        unsigned char *binary_sha1 = (unsigned char*)ptr;
        for (int i = 0; i < 20; i++) {
            sprintf(sha1_hex + (i * 2), "%02x", binary_sha1[i]);
        }
        sha1_hex[40] = '\0';

        ptr += 20;

        // Determine entry type from mode
        const char *type = (strcmp(mode, "040000") == 0) ? "tree" : "blob";

        // Create tree entry
        tree_entry *entry = tree_entry_new(mode, type, sha1_hex, name);
        if (!entry) {
            printf("ERROR: tree_entry_new: %d\n", -1);
            break;
        }
        tree_entry_add(entries, entry);
    }

    object_free(&obj);
    return 0;
}

static int tree_serialize(tree_entry *entries, char **data_out, size_t *size_out) {
    size_t tree_size = 0;
    for (tree_entry *current = entries; current; current = current->next) {
        tree_size += strlen(current->mode) + 1 + strlen(current->name) + 1 + 20;
    }

    char *tree_data = malloc(tree_size);
    if (!tree_data) {
        printf("ERROR: malloc: %d\n", -1);
        return -1;
    }

    char *ptr = tree_data;
    for (tree_entry *current = entries; current; current = current->next) {
        int mode_len = strlen(current->mode);
        int name_len = strlen(current->name);

        memcpy(ptr, current->mode, mode_len);
        ptr += mode_len;
        *ptr++ = ' ';

        memcpy(ptr, current->name, name_len);
        ptr += name_len;
        *ptr++ = '\0';

        unsigned char binary_sha1[20];
        for (int i = 0; i < 20; i++) {
            sscanf(current->sha1 + i * 2, "%2hhx", &binary_sha1[i]);
        }
        memcpy(ptr, binary_sha1, 20);
        ptr += 20;
    }

    *data_out = tree_data;
    *size_out = tree_size;
    return 0;
}

// Write tree from entries
int tree_write(tree_entry *entries, char *sha1_out) {
    int err;
    char *tree_data;
    size_t tree_size;
    if ((err = tree_serialize(entries, &tree_data, &tree_size)) != 0) {
        printf("ERROR: tree_serialize: %d\n", err);
        return err;
    }

    if ((err = object_write("tree", tree_data, tree_size, sha1_out)) != 0) {
        printf("ERROR: object_write: %d\n", err);
        free(tree_data);
        return err;
    }
    free(tree_data);
    return 0;
}

// Find entry in tree
tree_entry *tree_find(tree_entry *entries, const char *name) {
    while (entries) {
        if (strcmp(entries->name, name) == 0) {
            return entries;
        }
        entries = entries->next;
    }
    return NULL;
}

// Extract a blob object to the filesystem
static int extract_blob(const char *sha1, const char *target_path) {
    int err;
    char *data = NULL;
    size_t size = 0;

    if ((err = blob_read(sha1, &data, &size)) != 0) {
        printf("ERROR: blob_read: %d\n", err);
        return err;
    }

    // Create directory if needed
    char *dir_path = strdup(target_path);
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

    // Write file
    if ((err = write_file(target_path, data, size)) != 0) {
        printf("ERROR: write_file: %d\n", err);
        free(data);
        return err;
    }
    free(data);

    return 0;
}

// Extract a tree object recursively to the filesystem
static int extract_tree_recursive(const char *tree_sha1, const char *base_path) {
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
            // Extract file
            if ((err = extract_blob(current->sha1, full_path)) != 0) {
                printf("ERROR: extract_blob: %d\n", err);
                tree_free(entries);
                return err;
            }
        } else if (strcmp(current->type, "tree") == 0) {
            // Create directory and recurse
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

// Restore tree to working directory
int tree_restore(const char *tree_sha1, const char *target_dir) {
    int err;
    if (!tree_sha1 || !target_dir) {
        return -1;
    }

    // Save current directory
    char *original_dir = getcwd(NULL, 0);
    if (!original_dir) {
        printf("ERROR: getcwd: %d\n", -1);
        return -1;
    }

    // Change to target directory
    if (chdir(target_dir) != 0) {
        printf("ERROR: chdir: %d\n", -1);
        free(original_dir);
        return -1;
    }

    if ((err = extract_tree_recursive(tree_sha1, "")) != 0) {
        chdir(original_dir);
        free(original_dir);
        return err;
    }

    // Restore original directory
    chdir(original_dir);
    free(original_dir);

    return 0;
}