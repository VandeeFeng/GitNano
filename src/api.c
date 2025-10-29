#include "gitnano.h"

// Helper structure for file comparison
typedef struct file_entry {
    char path[MAX_PATH];
    char sha1[SHA1_HEX_SIZE];
    struct file_entry *next;
} file_entry;

// Helper functions for diff functionality
static file_entry *collect_tree_files(const char *tree_sha1);
static int add_file_to_list(file_entry **list, const char *path, const char *sha1);
static file_entry *find_file(file_entry *list, const char *path);
static void free_file_list(file_entry *list);
static int compare_trees(const char *tree1_sha1, const char *tree2_sha1, gitnano_diff_result *diff);

// High-level API for other applications

// Create a snapshot of the current directory
int gitnano_create_snapshot(const char *message, char *snapshot_id) {
    if (!file_exists(GITNANO_DIR)) {
        // Auto-initialize if repository doesn't exist
        if (gitnano_init() != 0) {
            return -1;
        }
    }

    // Build tree from current directory
    char tree_sha1[SHA1_HEX_SIZE];
    if (tree_build(".", tree_sha1) != 0) {
        return -1;
    }

    // Get parent commit
    char parent_sha1[SHA1_HEX_SIZE] = {0};
    get_current_commit(parent_sha1);

    // Create commit
    char commit_sha1[SHA1_HEX_SIZE];
    if (commit_create(tree_sha1, strlen(parent_sha1) > 0 ? parent_sha1 : NULL,
                      NULL, message, commit_sha1) != 0) {
        return -1;
    }

    // Update HEAD
    char ref[MAX_PATH];
    get_head_ref(ref);
    if (strncmp(ref, "refs/heads/", 11) == 0) {
        write_file(ref, commit_sha1, strlen(commit_sha1));
    } else {
        set_head_ref(commit_sha1);
    }

    if (snapshot_id) {
        strcpy(snapshot_id, commit_sha1);
    }

    return 0;
}

// List all snapshots
int gitnano_list_snapshots(gitnano_snapshot_info **snapshots, int *count) {
    *snapshots = NULL;
    *count = 0;

    if (!file_exists(GITNANO_DIR)) {
        return -1;
    }

    // Start from current commit and walk backwards
    char current_sha1[SHA1_HEX_SIZE];
    if (get_current_commit(current_sha1) != 0) {
        return 0; // No commits
    }

    // Count commits first
    int temp_count = 0;
    char temp_sha1[SHA1_HEX_SIZE];
    strcpy(temp_sha1, current_sha1);

    while (strlen(temp_sha1) > 0) {
        temp_count++;
        if (commit_get_parent(temp_sha1, temp_sha1) != 0) {
            break;
        }
    }

    if (temp_count == 0) {
        return 0;
    }

    // Allocate memory
    *snapshots = malloc(temp_count * sizeof(gitnano_snapshot_info));
    if (!*snapshots) {
        return -1;
    }

    // Fill snapshot information
    int index = 0;
    strcpy(current_sha1, temp_sha1);
    strcpy(current_sha1, temp_sha1);

    // Reset to beginning
    get_current_commit(current_sha1);

    while (strlen(current_sha1) > 0 && index < temp_count) {
        gitnano_commit_info commit;
        if (commit_parse(current_sha1, &commit) != 0) {
            break;
        }

        strcpy((*snapshots)[index].id, current_sha1);
        snprintf((*snapshots)[index].message,
                 sizeof((*snapshots)[index].message), "%s", commit.message);
        snprintf((*snapshots)[index].author,
                 sizeof((*snapshots)[index].author), "%s", commit.author);
        snprintf((*snapshots)[index].timestamp,
                 sizeof((*snapshots)[index].timestamp), "%s", commit.timestamp);
        (*snapshots)[index].tree_hash[0] = '\0';
        commit_get_tree(current_sha1, (*snapshots)[index].tree_hash);

        index++;

        if (commit_get_parent(current_sha1, current_sha1) != 0) {
            break;
        }
    }

    *count = index;
    return 0;
}

// Restore to a specific snapshot
int gitnano_restore_snapshot(const char *snapshot_id) {
    if (!file_exists(GITNANO_DIR)) {
        return -1;
    }

    if (!commit_exists(snapshot_id)) {
        return -1;
    }

    return gitnano_checkout(snapshot_id);
}

// Get file content at specific snapshot
int gitnano_get_file_at_snapshot(const char *snapshot_id, const char *file_path,
                                 char **content, size_t *size) {
    if (!file_exists(GITNANO_DIR)) {
        return -1;
    }

    if (!commit_exists(snapshot_id)) {
        return -1;
    }

    // Get tree from commit
    char tree_sha1[SHA1_HEX_SIZE];
    if (commit_get_tree(snapshot_id, tree_sha1) != 0) {
        return -1;
    }

    // Parse tree to find file
    tree_entry *entries;
    if (tree_parse(tree_sha1, &entries) != 0) {
        return -1;
    }

    // This is a simplified implementation - in reality, we'd need to
    // traverse the tree structure recursively
    tree_entry *entry = tree_find(entries, file_path);
    if (!entry) {
        tree_free(entries);
        return -1;
    }

    // Read blob content
    int result = blob_read(entry->sha1, content, size);
    tree_free(entries);

    return result;
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
static file_entry *collect_tree_files(const char *tree_sha1) {
    file_entry *files = NULL;
    tree_entry *entries = NULL;

    if (tree_parse(tree_sha1, &entries) != 0) {
        return NULL;
    }

    tree_entry *current = entries;
    while (current) {
        if (strcmp(current->type, "blob") == 0) {
            add_file_to_list(&files, current->name, current->sha1);
        } else if (strcmp(current->type, "tree") == 0) {
            // For subdirectories, we would need to recursively collect files
            // For now, just note the directory
            char subdir_path[MAX_PATH];
            int name_len = strlen(current->name);
            if (name_len + 2 < MAX_PATH) {  // +2 for "/" and null terminator
                strcpy(subdir_path, current->name);
                strcat(subdir_path, "/");
                add_file_to_list(&files, subdir_path, current->sha1);
            }
        }
        current = current->next;
    }

    tree_free(entries);
    return files;
}

// Compare two trees and populate diff result
static int compare_trees(const char *tree1_sha1, const char *tree2_sha1, gitnano_diff_result *diff) {
    file_entry *files1 = collect_tree_files(tree1_sha1);
    file_entry *files2 = collect_tree_files(tree2_sha1);

    if (!files1 && !files2) {
        return 0; // Both trees are empty
    }

    // Temporary arrays to store diff results
    char **added = NULL;
    char **modified = NULL;
    char **deleted = NULL;
    int added_count = 0, modified_count = 0, deleted_count = 0;

    // Find added files (in files2 but not in files1)
    file_entry *current = files2;
    while (current) {
        file_entry *match = find_file(files1, current->path);
        if (!match) {
            added = realloc(added, (added_count + 1) * sizeof(char*));
            added[added_count] = malloc(strlen(current->path) + 1);
            strcpy(added[added_count], current->path);
            added_count++;
        }
        current = current->next;
    }

    // Find deleted files (in files1 but not in files2)
    current = files1;
    while (current) {
        file_entry *match = find_file(files2, current->path);
        if (!match) {
            deleted = realloc(deleted, (deleted_count + 1) * sizeof(char*));
            deleted[deleted_count] = malloc(strlen(current->path) + 1);
            strcpy(deleted[deleted_count], current->path);
            deleted_count++;
        }
        current = current->next;
    }

    // Find modified files (in both but with different SHA1)
    current = files2;
    while (current) {
        file_entry *match = find_file(files1, current->path);
        if (match && strcmp(match->sha1, current->sha1) != 0) {
            modified = realloc(modified, (modified_count + 1) * sizeof(char*));
            modified[modified_count] = malloc(strlen(current->path) + 1);
            strcpy(modified[modified_count], current->path);
            modified_count++;
        }
        current = current->next;
    }

    // Populate diff result
    diff->added_files = added;
    diff->added_count = added_count;
    diff->modified_files = modified;
    diff->modified_count = modified_count;
    diff->deleted_files = deleted;
    diff->deleted_count = deleted_count;

    free_file_list(files1);
    free_file_list(files2);

    return 0;
}

// Compare two snapshots
int gitnano_compare_snapshots(const char *snapshot1, const char *snapshot2,
                             gitnano_diff_result **diff) {
    if (!snapshot1 || !snapshot2 || !diff) {
        return -1;
    }

    *diff = malloc(sizeof(gitnano_diff_result));
    if (!*diff) return -1;

    (*diff)->added_files = NULL;
    (*diff)->modified_files = NULL;
    (*diff)->deleted_files = NULL;
    (*diff)->added_count = 0;
    (*diff)->modified_count = 0;
    (*diff)->deleted_count = 0;

    // Get tree SHA1s from commits
    char tree1_sha1[SHA1_HEX_SIZE];
    char tree2_sha1[SHA1_HEX_SIZE];

    if (commit_get_tree(snapshot1, tree1_sha1) != 0) {
        return -1;
    }

    if (commit_get_tree(snapshot2, tree2_sha1) != 0) {
        return -1;
    }

    // Compare the trees
    return compare_trees(tree1_sha1, tree2_sha1, *diff);
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