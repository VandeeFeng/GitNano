#define _GNU_SOURCE
#include "gitnano.h"
#include <dirent.h>

static int tree_serialize(tree_entry *entries, char **data_out, size_t *size_out);

// Create a new tree entry
tree_entry *tree_entry_new(const char *mode, const char *type,
                           const char *sha1, const char *name) {
    tree_entry *entry = malloc(sizeof(tree_entry));
    if (!entry) return NULL;

    strncpy(entry->mode, mode, sizeof(entry->mode) - 1);
    entry->mode[sizeof(entry->mode) - 1] = '\0';

    strncpy(entry->type, type, sizeof(entry->type) - 1);
    entry->type[sizeof(entry->type) - 1] = '\0';

    strncpy(entry->sha1, sha1, sizeof(entry->sha1) - 1);
    entry->sha1[sizeof(entry->sha1) - 1] = '\0';

    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';

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

// Helper function to convert hex SHA1 to binary
static void hex_to_binary(const char *hex, unsigned char *binary) {
    for (int i = 0; i < 20; i++) {
        sscanf(hex + i * 2, "%2hhx", &binary[i]);
    }
}

static int tree_serialize(tree_entry *entries, char **data_out, size_t *size_out) {
    // Calculate total size needed
    size_t tree_size = 0;
    for (tree_entry *current = entries; current; current = current->next) {
        tree_size += strlen(current->mode) + 1 + strlen(current->name) + 1 + 20;
    }

    char *tree_data = safe_malloc(tree_size);
    if (!tree_data) {
        return -1;
    }

    char *ptr = tree_data;
    for (tree_entry *current = entries; current; current = current->next) {
        // Copy mode
        int mode_len = strlen(current->mode);
        memcpy(ptr, current->mode, mode_len);
        ptr += mode_len;
        *ptr++ = ' ';

        // Copy name
        int name_len = strlen(current->name);
        memcpy(ptr, current->name, name_len);
        ptr += name_len;
        *ptr++ = '\0';

        // Convert and copy SHA1
        unsigned char binary_sha1[20];
        hex_to_binary(current->sha1, binary_sha1);
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


// Find file entry by path in tree
static tree_entry *find_entry_by_path(tree_entry *entries, const char *path) {
    if (!path || strlen(path) == 0) {
        return NULL;
    }

    char *path_copy = safe_strdup(path);
    char *save_ptr;
    char *token = strtok_r(path_copy, "/", &save_ptr);
    tree_entry *current = entries;

    while (token && current) {
        // Find matching entry for current path component
        tree_entry *found = NULL;
        while (current) {
            if (strcmp(current->name, token) == 0) {
                found = current;
                break;
            }
            current = current->next;
        }

        if (!found) {
            free(path_copy);
            return NULL;
        }

        token = strtok_r(NULL, "/", &save_ptr);
        if (token) {
            // Need to go deeper - parse the subtree
            if (strcmp(found->type, "tree") != 0) {
                free(path_copy);
                return NULL;
            }

            tree_entry *subtree = NULL;
            if (tree_parse(found->sha1, &subtree) != 0) {
                free(path_copy);
                return NULL;
            }

            tree_free(entries);
            entries = subtree;
            current = subtree;
        } else {
            // Found the target entry
            free(path_copy);
            return found;
        }
    }

    free(path_copy);
    return NULL;
}

// Restore specific path from tree
int tree_restore_path(const char *tree_sha1, const char *tree_path, const char *target_path) {
    int err;
    if (!tree_sha1 || !tree_path || !target_path) {
        return -1;
    }

    tree_entry *entries = NULL;
    if ((err = tree_parse(tree_sha1, &entries)) != 0) {
        printf("ERROR: tree_parse: %d\n", err);
        return err;
    }

    tree_entry *target_entry = find_entry_by_path(entries, tree_path);
    if (!target_entry) {
        tree_free(entries);
        printf("Path not found in tree: %s\n", tree_path);
        return -1;
    }

    if (strcmp(target_entry->type, "blob") == 0) {
        // Restore single file
        if ((err = extract_blob(target_entry->sha1, target_path)) != 0) {
            printf("ERROR: extract_blob: %d\n", err);
            tree_free(entries);
            return err;
        }
    } else if (strcmp(target_entry->type, "tree") == 0) {
        // Restore directory recursively
        if ((err = extract_tree_recursive(target_entry->sha1, target_path)) != 0) {
            printf("ERROR: extract_tree_recursive: %d\n", err);
            tree_free(entries);
            return err;
        }
    }

    tree_free(entries);
    return 0;
}

// Free checkout operation statistics
void free_checkout_stats(checkout_operation_stats *stats) {
    if (!stats) return;

    for (int i = 0; i < stats->added_count; i++) {
        free(stats->added_files[i]);
    }
    for (int i = 0; i < stats->modified_count; i++) {
        free(stats->modified_files[i]);
    }
    for (int i = 0; i < stats->deleted_count; i++) {
        free(stats->deleted_files[i]);
    }

    free(stats->added_files);
    free(stats->modified_files);
    free(stats->deleted_files);

    memset(stats, 0, sizeof(checkout_operation_stats));
}

// Print checkout operation summary
void print_checkout_summary(const checkout_operation_stats *stats) {
    if (!stats) return;

    int total_operations = stats->added_count + stats->modified_count + stats->deleted_count;

    if (total_operations == 0) {
        printf("Already up to date.\n");
        return;
    }

    printf("Summary of changes:\n");

    if (stats->added_count > 0) {
        printf("  Added: %d file%s\n", stats->added_count,
               stats->added_count == 1 ? "" : "s");
        for (int i = 0; i < stats->added_count; i++) {
            printf("    + %s\n", stats->added_files[i]);
        }
    }

    if (stats->modified_count > 0) {
        printf("  Modified: %d file%s\n", stats->modified_count,
               stats->modified_count == 1 ? "" : "s");
        for (int i = 0; i < stats->modified_count; i++) {
            printf("    M %s\n", stats->modified_files[i]);
        }
    }

    if (stats->deleted_count > 0) {
        printf("  Deleted: %d file%s\n", stats->deleted_count,
               stats->deleted_count == 1 ? "" : "s");
        for (int i = 0; i < stats->deleted_count; i++) {
            printf("    - %s\n", stats->deleted_files[i]);
        }
    }
}

// Enhanced tree restore with statistics
int tree_restore(const char *tree_sha1, const char *target_dir) {
    int err;
    if (!tree_sha1 || !target_dir) {
        return -1;
    }

    printf("Restoring tree ");
    print_colored_hash(tree_sha1);
    printf(" to %s...\n", target_dir);

    // First, collect target files for cleanup operations
    file_entry *target_files = NULL;
    if ((err = collect_target_files(tree_sha1, "", &target_files)) != 0) {
        printf("ERROR: collect_target_files: %d\n", err);
        return err;
    }

    // Extract tree files (create/update files and directories)
    printf("Extracting files from tree...\n");
    if ((err = extract_tree_recursive(tree_sha1, target_dir)) != 0) {
        printf("ERROR: extract_tree_recursive: %d\n", err);
        free_file_list(target_files);
        return err;
    }

    // Clean up files not in target tree (delete files that shouldn't exist)
    printf("Cleaning up files not in target tree...\n");
    if ((err = cleanup_extra_files(target_dir, target_files)) != 0) {
        printf("ERROR: cleanup_extra_files: %d\n", err);
        free_file_list(target_files);
        return err;
    }

    free_file_list(target_files);
    printf("Tree restore completed successfully\n");
    return 0;
}
