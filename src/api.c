#include "gitnano.h"

// Helper structure for file comparison
typedef struct file_entry {
    char path[MAX_PATH];
    char sha1[SHA1_HEX_SIZE];
    struct file_entry *next;
} file_entry;

// Helper functions for diff functionality
static int collect_tree_files(const char *tree_sha1, file_entry **files_out);
static int add_file_to_list(file_entry **list, const char *path, const char *sha1);
static file_entry *find_file(file_entry *list, const char *path);
static void free_file_list(file_entry *list);
static int compare_trees(const char *tree1_sha1, const char *tree2_sha1, gitnano_diff_result *diff);

// High-level API for other applications

// Create a snapshot of the current directory
int gitnano_create_snapshot(const char *message, char *snapshot_id) {
    int err;
    if (!file_exists(GITNANO_DIR)) {
        // Auto-initialize if repository doesn't exist
        if ((err = gitnano_init()) != 0) {
            printf("ERROR: gitnano_init: %d\n", err);
            return err;
        }
    }

    // Build tree from current directory
    char tree_sha1[SHA1_HEX_SIZE];
    if ((err = tree_build(".", tree_sha1)) != 0) {
        printf("ERROR: tree_build: %d\n", err);
        return err;
    }

    // Get parent commit
    char parent_sha1[SHA1_HEX_SIZE] = {0};
    get_current_commit(parent_sha1);

    // Create commit
    char commit_sha1[SHA1_HEX_SIZE];
    if ((err = commit_create(tree_sha1, strlen(parent_sha1) > 0 ? parent_sha1 : NULL,
                      NULL, message, commit_sha1)) != 0) {
        printf("ERROR: commit_create: %d\n", err);
        return err;
    }

    // Update HEAD
    char ref[MAX_PATH];
    get_head_ref(ref);
    if (strncmp(ref, "refs/heads/", 11) == 0) {
        if ((err = write_file(ref, commit_sha1, strlen(commit_sha1))) != 0) {
            printf("ERROR: write_file: %d\n", err);
            return err;
        }
    } else {
        if ((err = set_head_ref(commit_sha1)) != 0) {
            printf("ERROR: set_head_ref: %d\n", err);
            return err;
        }
    }

    if (snapshot_id) {
        strcpy(snapshot_id, commit_sha1);
    }

    return 0;
}

// List all snapshots
int gitnano_list_snapshots(gitnano_snapshot_info **snapshots, int *count) {
    int err;
    *snapshots = NULL;
    *count = 0;

    if (!file_exists(GITNANO_DIR)) {
        return -1;
    }

    char current_sha1[SHA1_HEX_SIZE];
    if (get_current_commit(current_sha1) != 0) {
        return 0; // No commits
    }

    int capacity = 10;
    *snapshots = malloc(capacity * sizeof(gitnano_snapshot_info));
    if (!*snapshots) {
        printf("ERROR: malloc failed\n");
        return -1;
    }

    while (strlen(current_sha1) > 0) {
        if (*count >= capacity) {
            capacity *= 2;
            gitnano_snapshot_info *new_snapshots = realloc(*snapshots, capacity * sizeof(gitnano_snapshot_info));
            if (!new_snapshots) {
                printf("ERROR: realloc failed\n");
                free(*snapshots);
                *snapshots = NULL;
                return -1;
            }
            *snapshots = new_snapshots;
        }

        gitnano_commit_info commit;
        if ((err = commit_parse(current_sha1, &commit)) != 0) {
            printf("ERROR: commit_parse: %d\n", err);
            break;
        }

        gitnano_snapshot_info *snapshot = &(*snapshots)[*count];
        strcpy(snapshot->id, current_sha1);
        strncpy(snapshot->message, commit.message, sizeof(snapshot->message) - 1);
        snapshot->message[sizeof(snapshot->message) - 1] = '\0';
        strncpy(snapshot->author, commit.author, sizeof(snapshot->author) - 1);
        snapshot->author[sizeof(snapshot->author) - 1] = '\0';
        strncpy(snapshot->timestamp, commit.timestamp, sizeof(snapshot->timestamp) - 1);
        snapshot->timestamp[sizeof(snapshot->timestamp) - 1] = '\0';
        if ((err = commit_get_tree(current_sha1, snapshot->tree_hash)) != 0) {
            printf("ERROR: commit_get_tree: %d\n", err);
            break;
        }

        (*count)++;

        if (commit_get_parent(current_sha1, current_sha1) != 0) {
            break;
        }
    }

    return 0;
}

// Restore to a specific snapshot
int gitnano_restore_snapshot(const char *snapshot_id) {
    int err;
    if (!file_exists(GITNANO_DIR)) {
        return -1;
    }

    if (!commit_exists(snapshot_id)) {
        return -1;
    }

    if ((err = gitnano_checkout(snapshot_id)) != 0) {
        printf("ERROR: gitnano_checkout: %d\n", err);
        return err;
    }
    return 0;
}

// Get file content at specific snapshot
int gitnano_get_file_at_snapshot(const char *snapshot_id, const char *file_path,
                                 char **content, size_t *size) {
    int err;
    if (!file_exists(GITNANO_DIR)) {
        return -1;
    }

    if (!commit_exists(snapshot_id)) {
        return -1;
    }

    // Get tree from commit
    char tree_sha1[SHA1_HEX_SIZE];
    if ((err = commit_get_tree(snapshot_id, tree_sha1)) != 0) {
        printf("ERROR: commit_get_tree: %d\n", err);
        return err;
    }

    // Parse tree to find file
    tree_entry *entries;
    if ((err = tree_parse(tree_sha1, &entries)) != 0) {
        printf("ERROR: tree_parse: %d\n", err);
        return err;
    }

    // This is a simplified implementation - in reality, we'd need to
    // traverse the tree structure recursively
    tree_entry *entry = tree_find(entries, file_path);
    if (!entry) {
        tree_free(entries);
        return -1;
    }

    // Read blob content
    if ((err = blob_read(entry->sha1, content, size)) != 0) {
        printf("ERROR: blob_read: %d\n", err);
        tree_free(entries);
        return err;
    }
    tree_free(entries);

    return 0;
}

// Add file to file list
static int add_file_to_list(file_entry **list, const char *path, const char *sha1) {
    file_entry *entry = malloc(sizeof(file_entry));
    if (!entry) return -1;

    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';
    strncpy(entry->sha1, sha1, sizeof(entry->sha1) - 1);
    entry->sha1[sizeof(entry->sha1) - 1] = '\0';
    entry->next = *list;
    *list = entry;

    return 0;
}

// Find file in list
static file_entry *find_file(file_entry *list, const char *path) {
    while (list) {
        if (strcmp(list->path, path) == 0) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}

// Free file list
static void free_file_list(file_entry *list) {
    while (list) {
        file_entry *next = list->next;
        free(list);
        list = next;
    }
}

// Recursively collect all files from a tree
static int collect_tree_files(const char *tree_sha1, file_entry **files_out) {
    int err;
    *files_out = NULL;
    tree_entry *entries = NULL;

    if ((err = tree_parse(tree_sha1, &entries)) != 0) {
        printf("ERROR: tree_parse: %d\n", err);
        return err;
    }

    tree_entry *current = entries;
    while (current) {
        if (strcmp(current->type, "blob") == 0) {
            if ((err = add_file_to_list(files_out, current->name, current->sha1)) != 0) {
                printf("ERROR: add_file_to_list: %d\n", err);
                goto cleanup;
            }
        } else if (strcmp(current->type, "tree") == 0) {
            // For subdirectories, we would need to recursively collect files
            // For now, just note the directory
            char subdir_path[MAX_PATH];
            int name_len = strlen(current->name);
            if (name_len + 2 < MAX_PATH) {  // +2 for "/" and null terminator
                strcpy(subdir_path, current->name);
                strcat(subdir_path, "/");
                if ((err = add_file_to_list(files_out, subdir_path, current->sha1)) != 0) {
                    printf("ERROR: add_file_to_list: %d\n", err);
                    goto cleanup;
                }
            }
        }
        current = current->next;
    }

    tree_free(entries);
    return 0;

cleanup:
    tree_free(entries);
    free_file_list(*files_out);
    *files_out = NULL;
    return err;
}

// Compare two trees and populate diff result
static int compare_trees(const char *tree1_sha1, const char *tree2_sha1, gitnano_diff_result *diff) {
    int err;
    file_entry *files1 = NULL, *files2 = NULL;

    if ((err = collect_tree_files(tree1_sha1, &files1)) != 0) return err;
    if ((err = collect_tree_files(tree2_sha1, &files2)) != 0) {
        free_file_list(files1);
        return err;
    }

    char **added = NULL, **modified = NULL, **deleted = NULL;
    int added_count = 0, modified_count = 0, deleted_count = 0;

    // Mark files from the second list to track matches
    for (file_entry *f2 = files2; f2; f2 = f2->next) {
        f2->sha1[0] &= 0x7F; // Use a bit to mark as not found yet
    }

    // Identify deleted and modified files
    for (file_entry *f1 = files1; f1; f1 = f1->next) {
        file_entry *f2 = find_file(files2, f1->path);
        if (f2) {
            f2->sha1[0] |= 0x80; // Mark as found
            if (strcmp(f1->sha1, f2->sha1) != 0) {
                modified = realloc(modified, (modified_count + 1) * sizeof(char*));
                if (!modified) goto nomem;
                modified[modified_count] = strdup(f1->path);
                if (!modified[modified_count]) goto nomem;
                modified_count++;
            }
        } else {
            deleted = realloc(deleted, (deleted_count + 1) * sizeof(char*));
            if (!deleted) goto nomem;
            deleted[deleted_count] = strdup(f1->path);
            if (!deleted[deleted_count]) goto nomem;
            deleted_count++;
        }
    }

    // Identify added files
    for (file_entry *f2 = files2; f2; f2 = f2->next) {
        if ((f2->sha1[0] & 0x80) == 0) {
            added = realloc(added, (added_count + 1) * sizeof(char*));
            if (!added) goto nomem;
            added[added_count] = strdup(f2->path);
            if (!added[added_count]) goto nomem;
            added_count++;
        }
        f2->sha1[0] &= 0x7F; // Restore original SHA1
    }

    diff->added_files = added;
    diff->added_count = added_count;
    diff->modified_files = modified;
    diff->modified_count = modified_count;
    diff->deleted_files = deleted;
    diff->deleted_count = deleted_count;

    free_file_list(files1);
    free_file_list(files2);

    return 0;

nomem:
    printf("ERROR: Out of memory in compare_trees\n");
    // Free any allocated memory before returning
    for (int i = 0; i < added_count; i++) free(added[i]);
    for (int i = 0; i < modified_count; i++) free(modified[i]);
    for (int i = 0; i < deleted_count; i++) free(deleted[i]);
    free(added);
    free(modified);
    free(deleted);
    free_file_list(files1);
    free_file_list(files2);
    return -1;
}

// Compare two snapshots
int gitnano_compare_snapshots(const char *snapshot1, const char *snapshot2,
                             gitnano_diff_result **diff) {
    int err;
    if (!snapshot1 || !snapshot2 || !diff) {
        return -1;
    }

    *diff = malloc(sizeof(gitnano_diff_result));
    if (!*diff) {
        printf("ERROR: malloc failed\n");
        return -1;
    }
    memset(*diff, 0, sizeof(gitnano_diff_result));

    char tree1_sha1[SHA1_HEX_SIZE];
    char tree2_sha1[SHA1_HEX_SIZE];

    if ((err = commit_get_tree(snapshot1, tree1_sha1)) != 0) {
        printf("ERROR: commit_get_tree: %d\n", err);
        goto cleanup;
    }

    if ((err = commit_get_tree(snapshot2, tree2_sha1)) != 0) {
        printf("ERROR: commit_get_tree: %d\n", err);
        goto cleanup;
    }

    if ((err = compare_trees(tree1_sha1, tree2_sha1, *diff)) != 0) {
        goto cleanup;
    }

    return 0;

cleanup:
    free(*diff);
    *diff = NULL;
    return err;
}

// Free diff result
void gitnano_free_diff(gitnano_diff_result *diff) {
    if (!diff) return;

    // Free individual file names
    for (int i = 0; i < diff->added_count; i++) {
        free(diff->added_files[i]);
    }
    for (int i = 0; i < diff->modified_count; i++) {
        free(diff->modified_files[i]);
    }
    for (int i = 0; i < diff->deleted_count; i++) {
        free(diff->deleted_files[i]);
    }

    // Free arrays
    free(diff->added_files);
    free(diff->modified_files);
    free(diff->deleted_files);
    free(diff);
}

// Get repository status
int gitnano_status(gitnano_status_info *status) {
    if (!status) return -1;

    memset(status, 0, sizeof(gitnano_status_info));

    if (!file_exists(GITNANO_DIR)) {
        status->is_repo = 0;
        return 0;
    }

    status->is_repo = 1;

    // Get current commit
    char current_sha1[SHA1_HEX_SIZE];
    if (get_current_commit(current_sha1) == 0) {
        strcpy(status->current_commit, current_sha1);
        status->has_commits = 1;
    }

    // Check for staged files (simplified)
    status->staged_files = 0;
    if (file_exists(INDEX_FILE)) {
        // Count lines in index file
        size_t size;
        char *content = read_file(INDEX_FILE, &size);
        if (content) {
            for (size_t i = 0; i < size; i++) {
                if (content[i] == '\n') {
                    status->staged_files++;
                }
            }
            free(content);
        }
    }

    status->current_branch[0] = '\0';
    char ref[MAX_PATH];
    if (get_head_ref(ref) == 0 && strncmp(ref, "refs/heads/", 11) == 0) {
        strcpy(status->current_branch, ref + 11);
    }

    return 0;
}

// Cleanup function
void gitnano_cleanup() {
    // Free any global resources if needed
    // Currently not needed for this implementation
}