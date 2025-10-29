#define _GNU_SOURCE
#include "gitnano.h"
#include <dirent.h>

static int check_repo_exists();
static int auto_sync_working_files();
static int collect_tree_files(const char *tree_sha1, file_entry **files_out);
static file_entry *find_file_in_list(file_entry *list, const char *path);

static int check_repo_exists() {
    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        return -1;
    }

    char *gitnano_dir = safe_asprintf("%s/.gitnano", workspace_path);

    if (!file_exists(gitnano_dir)) {
        free(gitnano_dir);
        printf("Not a GitNano repository (workspace not initialized)\n");
        return -1;
    }
    free(gitnano_dir);
    return 0;
}

// Initialize GitNano repository - create workspace structure only
int gitnano_init() {
    // Initialize workspace with .gitnano structure only (no file copying)
    if (workspace_init() != 0) {
        printf("ERROR: Failed to initialize GitNano repository\n");
        return -1;
    }

    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        printf("ERROR: Failed to get workspace path\n");
        return -1;
    }

    printf("Initialized GitNano repository\n");
    printf("Workspace location: %s\n", workspace_path);
    printf("Files will be added to workspace when you run 'gitnano add'\n");
    return 0;
}

// Add file to staging area
int gitnano_add(const char *path) {
    int err;

    // Check if this is a valid GitNano repository
    if (check_repo_exists() != 0) {
        return -1;
    }

    // First sync the file to workspace
    if (workspace_push_file(path) != 0) {
        printf("ERROR: Failed to sync file to workspace: %s\n", path);
        return -1;
    }

    // Read file from original directory
    size_t size;
    char *data = read_file(path, &size);
    if (!data) {
        printf("Failed to read file: %s\n", path);
        return -1;
    }

    // Change to workspace directory for gitnano operations
    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        printf("ERROR: Failed to get workspace path\n");
        free(data);
        return -1;
    }

    char original_cwd[MAX_PATH];
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        printf("ERROR: Failed to get current directory\n");
        free(data);
        return -1;
    }

    if (chdir(workspace_path) != 0) {
        printf("ERROR: Failed to change to workspace directory\n");
        free(data);
        return -1;
    }

    char sha1[SHA1_HEX_SIZE];
    if ((err = blob_write(data, size, sha1)) != 0) {
        free(data);
        printf("ERROR: blob_write: %d\n", err);
        chdir(original_cwd);
        return err;
    }

    free(data);

    // Update index (simplified - just append)
    FILE *index_fp = fopen(INDEX_FILE, "a");
    if (!index_fp) {
        index_fp = fopen(INDEX_FILE, "w");
        if (!index_fp) {
            printf("ERROR: fopen index: %d\n", -1);
            chdir(original_cwd);
            return -1;
        }
    }

    fprintf(index_fp, "%s %s\n", sha1, path);
    fclose(index_fp);

    // Change back to original directory
    chdir(original_cwd);

    printf("Added %s (blob: ", path);
    print_colored_hash(sha1);
    printf(")\n");
    return 0;
}

// Create commit
int gitnano_commit(const char *message) {
    int err;
    if (check_repo_exists() != 0) return -1;

    if (!message || strlen(message) == 0) {
        printf("Commit message cannot be empty\n");
        return -1;
    }

    // Auto-sync working files to workspace before commit
    printf("Auto-syncing working files...\n");
    if ((err = auto_sync_working_files()) != 0) {
        printf("WARNING: Auto-sync failed: %d, proceeding with existing workspace files\n", err);
        // Continue anyway - user might have manually synced files
    }

    // Change to workspace directory for gitnano operations
    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        printf("ERROR: Failed to get workspace path\n");
        return -1;
    }

    char original_cwd[MAX_PATH];
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        printf("ERROR: Failed to get current directory\n");
        return -1;
    }

    if (chdir(workspace_path) != 0) {
        printf("ERROR: Failed to change to workspace directory\n");
        return -1;
    }

    char tree_sha1[SHA1_HEX_SIZE];
    if ((err = tree_build(".", tree_sha1)) != 0) {
        printf("ERROR: tree_build: %d\n", err);
        chdir(original_cwd);
        return err;
    }

    char parent_sha1[SHA1_HEX_SIZE] = {0};
    err = get_current_commit(parent_sha1);
    // Only use parent if it's a valid GitNano commit (not from .git/)
    if (err == 0 && strlen(parent_sha1) > 0) {
        // Verify this is actually a GitNano commit object
        if (!commit_exists(parent_sha1)) {
            printf("WARNING: Current HEAD points to non-GitNano commit, starting new history\n");
            parent_sha1[0] = '\0';
        }
    } else {
        parent_sha1[0] = '\0';
    }

    char commit_sha1[SHA1_HEX_SIZE];
    if ((err = commit_create(tree_sha1, strlen(parent_sha1) > 0 ? parent_sha1 : NULL,
                             NULL, message, commit_sha1)) != 0) {
        printf("ERROR: commit_create: %d\n", err);
        chdir(original_cwd);
        return err;
    }

    // Update HEAD
    char ref[MAX_PATH];
    if ((err = get_head_ref(ref)) != 0) {
        printf("ERROR: get_head_ref: %d\n", err);
        chdir(original_cwd);
        return err;
    }

    if (strncmp(ref, "refs/heads/", 11) == 0) {
        char full_path[MAX_PATH];
        int path_len = snprintf(full_path, sizeof(full_path), "%s/%s", GITNANO_DIR, ref);

        if (path_len >= MAX_PATH) {
            printf("ERROR: Path too long for branch reference\n");
            chdir(original_cwd);
            return -1;
        }

        char branch_content[SHA1_HEX_SIZE + 2];
        snprintf(branch_content, sizeof(branch_content), "%s\n", commit_sha1);
        if ((err = write_file(full_path, branch_content, strlen(branch_content))) != 0) {
            printf("ERROR: write_file: %d\n", err);
            chdir(original_cwd);
            return err;
        }
    } else {
        if ((err = set_head_ref(commit_sha1)) != 0) {
            printf("ERROR: set_head_ref: %d\n", err);
            chdir(original_cwd);
            return err;
        }
    }

    // Change back to original directory
    chdir(original_cwd);

    printf("Committed ");
    print_colored_hash(commit_sha1);
    printf("\n");
    return 0;
}

// Checkout function with support for references and paths
int gitnano_checkout(const char *reference, const char *path) {
    int err;
    if (check_repo_exists() != 0) return -1;

    if (!reference) {
        printf("ERROR: No reference specified\n");
        return -1;
    }

    // Change to workspace directory for gitnano operations
    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        printf("ERROR: Failed to get workspace path\n");
        return -1;
    }

    char original_cwd[MAX_PATH];
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        printf("ERROR: Failed to get current directory\n");
        return -1;
    }

    if (chdir(workspace_path) != 0) {
        printf("ERROR: Failed to change to workspace directory\n");
        return -1;
    }

    // Resolve reference to SHA1
    char commit_sha1[SHA1_HEX_SIZE];
    if ((err = resolve_reference(reference, commit_sha1)) != 0) {
        printf("ERROR: Invalid reference: %s\n", reference);
        chdir(original_cwd);
        return err;
    }

    if (!commit_exists(commit_sha1)) {
        printf("Commit not found: ");
        print_colored_hash(commit_sha1);
        printf("\n");
        chdir(original_cwd);
        return -1;
    }

    checkout_operation_stats stats = {0};

    if (path && strlen(path) > 0) {
        // Path checkout - restore specific file/directory in workspace
        printf("Restoring '%s' from %s...\n", path, reference);

        char tree_sha1[SHA1_HEX_SIZE];
        if ((err = commit_get_tree(commit_sha1, tree_sha1)) != 0) {
            printf("ERROR: commit_get_tree: %d\n", err);
            free_checkout_stats(&stats);
            chdir(original_cwd);
            return err;
        }

        if ((err = tree_restore_path(tree_sha1, path, path)) != 0) {
            printf("ERROR: tree_restore_path: %d\n", err);
            free_checkout_stats(&stats);
            chdir(original_cwd);
            return err;
        }

        // Change back to original directory to sync the restored file
        chdir(original_cwd);

        // Sync the restored file from workspace to original directory
        if ((err = workspace_pullback_file(path)) != 0) {
            printf("WARNING: Failed to sync restored file to original directory: %s\n", path);
        }

        printf("Restored %s from %s\n", path, reference);
    } else {
        // Full checkout - restore entire tree in workspace
        printf("Checking out %s...\n", reference);

        char tree_sha1[SHA1_HEX_SIZE];
        if ((err = commit_get_tree(commit_sha1, tree_sha1)) != 0) {
            printf("ERROR: commit_get_tree: %d\n", err);
            free_checkout_stats(&stats);
            chdir(original_cwd);
            return err;
        }

        if ((err = tree_restore(tree_sha1, ".")) != 0) {
            printf("ERROR: tree_restore: %d\n", err);
            free_checkout_stats(&stats);
            chdir(original_cwd);
            return err;
        }

        // Update HEAD to point to the checked out commit
        if ((err = set_head_ref(commit_sha1)) != 0) {
            printf("ERROR: set_head_ref: %d\n", err);
            free_checkout_stats(&stats);
            chdir(original_cwd);
            return err;
        }

        // Change back to original directory
        chdir(original_cwd);

        // Sync all restored files from workspace to original directory
        if ((err = workspace_sync_all_from_workspace()) != 0) {
            printf("WARNING: Failed to sync some files from workspace to original directory\n");
            // Continue anyway as the main checkout operation succeeded
        }

        // Clean up files in original directory that don't exist in the target commit
        file_entry *target_files = NULL;
        if ((err = collect_target_files(tree_sha1, "", &target_files)) != 0) {
            printf("WARNING: Failed to collect target files for cleanup\n");
        } else {
            if ((err = cleanup_extra_files(".", target_files)) != 0) {
                printf("WARNING: Failed to clean up extra files from original directory\n");
            }
            free_file_list(target_files);
        }

        printf("Checked out %s\n", reference);
    }

    free_checkout_stats(&stats);
    return 0;
}

// Show commit log
int gitnano_log() {
    int err;
    if (check_repo_exists() != 0) return -1;

    // Change to workspace directory for gitnano operations
    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        printf("ERROR: Failed to get workspace path\n");
        return -1;
    }

    char original_cwd[MAX_PATH];
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        printf("ERROR: Failed to get current directory\n");
        return -1;
    }

    if (chdir(workspace_path) != 0) {
        printf("ERROR: Failed to change to workspace directory\n");
        return -1;
    }

    char current_sha1[SHA1_HEX_SIZE];
    if ((err = get_current_commit(current_sha1)) != 0) {
        printf("No commits found\n");
        chdir(original_cwd);
        return 0;
    }

    printf("Commit history:\n");

    while (strlen(current_sha1) > 0) {
        gitnano_commit_info commit;
        if ((err = commit_parse(current_sha1, &commit)) != 0) {
            printf("ERROR: commit_parse: %d\n", err);
            break;
        }

        printf("\ncommit ");
        print_colored_hash(current_sha1);
        printf("\n");
        printf("Author: %s\n", commit.author);
        printf("Date:   %s\n", commit.timestamp);
        printf("\n    %s\n", commit.message);

        // Move to parent
        if (commit_get_parent(current_sha1, current_sha1) != 0) {
            // Failed to get parent - either no parent or parent is not a GitNano commit
            break;
        }

        // Verify the parent commit exists in GitNano repository
        if (!commit_exists(current_sha1)) {
            printf("WARNING: Parent commit %s not found in GitNano repository, stopping log\n", current_sha1);
            break;
        }
    }

    // Change back to original directory
    chdir(original_cwd);
    return 0;
}

// Show diff between commits
int gitnano_diff(const char *commit1, const char *commit2) {
    int err;
    if (check_repo_exists() != 0) return -1;

    // Change to workspace directory for gitnano operations
    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        printf("ERROR: Failed to get workspace path\n");
        return -1;
    }

    char original_cwd[MAX_PATH];
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        printf("ERROR: Failed to get current directory\n");
        return -1;
    }

    if (chdir(workspace_path) != 0) {
        printf("ERROR: Failed to change to workspace directory\n");
        return -1;
    }

    char sha1[SHA1_HEX_SIZE] = {0};
    char sha2[SHA1_HEX_SIZE] = {0};

    // Handle different argument scenarios
    if (!commit1 && !commit2) {
        if (get_current_commit(sha1) != 0) {
            printf("No commits found to compare\n");
            chdir(original_cwd);
            return -1;
        }
        printf("Comparing working directory with commit ");
        print_colored_hash(sha1);
        printf("\n");

        // Get current commit's tree first (while still in workspace)
        char commit_tree_sha1[SHA1_HEX_SIZE];
        if ((err = commit_get_tree(sha1, commit_tree_sha1)) != 0) {
            printf("ERROR: Failed to get tree from current commit: %d\n", err);
            chdir(original_cwd);
            return err;
        }

        // Now we have current commit's tree, let's implement simple file comparison
        printf("Working directory changes:\n");

        // Get current commit tree files (while still in workspace)
        file_entry *commit_files = NULL;
        int added_count = 0, modified_count = 0, deleted_count = 0;

        if (collect_tree_files(commit_tree_sha1, &commit_files) == 0) {
            // Change back to original directory to check working directory files
            if (chdir(original_cwd) != 0) {
                printf("ERROR: Failed to change back to original directory\n");
                free_file_list(commit_files);
                return -1;
            }

            // Check working directory files
            DIR *dir = opendir(".");
            if (dir) {
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    // Skip ., .., and .gitnano directory
                    if (strcmp(entry->d_name, ".") == 0 ||
                        strcmp(entry->d_name, "..") == 0 ||
                        strcmp(entry->d_name, ".gitnano") == 0) {
                        continue;
                    }

                    struct stat st;
                    if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                        // It's a regular file, check if it's in commit
                        file_entry *commit_file = find_file_in_list(commit_files, entry->d_name);
                        if (!commit_file) {
                            // New file
                            added_count++;
                        } else {
                            // File exists in commit, check if modified using system diff
                            char diff_cmd[MAX_PATH * 3];
                            char workspace_path[MAX_PATH];
                            get_workspace_path(workspace_path, sizeof(workspace_path));
                            snprintf(diff_cmd, sizeof(diff_cmd), "diff -q %s %s/%s >/dev/null 2>&1",
                                   entry->d_name, workspace_path, entry->d_name);

                            int result = system(diff_cmd);
                            if (result != 0) {
                                // File is modified
                                modified_count++;
                            }
                        }
                    }
                }
                closedir(dir);
            }

            // Check for deleted files
            file_entry *c = commit_files;
            while (c) {
                struct stat st;
                if (stat(c->path, &st) != 0) {
                    // File doesn't exist in working directory
                    deleted_count++;
                }
                c = c->next;
            }

            // Display results
            if (added_count > 0) {
                printf("\nAdded files (%d):\n", added_count);
                DIR *dir = opendir(".");
                if (dir) {
                    struct dirent *entry;
                    while ((entry = readdir(dir)) != NULL) {
                        if (strcmp(entry->d_name, ".") == 0 ||
                            strcmp(entry->d_name, "..") == 0 ||
                            strcmp(entry->d_name, ".gitnano") == 0) {
                            continue;
                        }
                        struct stat st;
                        if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                            if (!find_file_in_list(commit_files, entry->d_name)) {
                                printf("  + %s\n", entry->d_name);
                            }
                        }
                    }
                    closedir(dir);
                }
            }

            if (modified_count > 0) {
                printf("\nModified files (%d):\n", modified_count);
                DIR *dir = opendir(".");
                if (dir) {
                    struct dirent *entry;
                    while ((entry = readdir(dir)) != NULL) {
                        if (strcmp(entry->d_name, ".") == 0 ||
                            strcmp(entry->d_name, "..") == 0 ||
                            strcmp(entry->d_name, ".gitnano") == 0) {
                            continue;
                        }
                        struct stat st;
                        if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                            file_entry *commit_file = find_file_in_list(commit_files, entry->d_name);
                            if (commit_file) {
                                char diff_cmd[MAX_PATH * 3];
                                char workspace_path[MAX_PATH];
                                get_workspace_path(workspace_path, sizeof(workspace_path));
                                snprintf(diff_cmd, sizeof(diff_cmd), "diff -q %s %s/%s >/dev/null 2>&1",
                                       entry->d_name, workspace_path, entry->d_name);

                                int result = system(diff_cmd);
                                if (result != 0) {
                                    printf("  M %s\n", entry->d_name);
                                }
                            }
                        }
                    }
                    closedir(dir);
                }
            }

            if (deleted_count > 0) {
                printf("\nDeleted files (%d):\n", deleted_count);
                file_entry *c = commit_files;
                while (c) {
                    struct stat st;
                    if (stat(c->path, &st) != 0) {
                        printf("  - %s\n", c->path);
                    }
                    c = c->next;
                }
            }

            if (added_count == 0 && modified_count == 0 && deleted_count == 0) {
                printf("\nNo changes in working directory.\n");
            }

            free_file_list(commit_files);
        } else {
            printf("ERROR: Failed to get current commit files\n");
            return -1;
        }

    
        // Change back to original directory before returning
        if (chdir(original_cwd) != 0) {
            printf("ERROR: Failed to change back to original directory\n");
            return -1;
        }
        return 0;
    } else if (commit1 && !commit2) {
        // gitnano diff <sha1>: compare with current commit
        if (get_current_commit(sha2) != 0) {
            printf("No current commit found\n");
            chdir(original_cwd);
            return -1;
        }
        if (strlen(commit1) == SHA1_HEX_SIZE - 1) {
            strcpy(sha1, commit1);
        } else {
            printf("Invalid commit SHA1: %s\n", commit1);
            chdir(original_cwd);
            return -1;
        }
    } else if (commit1 && commit2) {
        // gitnano diff <sha1> <sha2>: compare two commits
        if (strlen(commit1) == SHA1_HEX_SIZE - 1 && strlen(commit2) == SHA1_HEX_SIZE - 1) {
            strcpy(sha1, commit1);
            strcpy(sha2, commit2);
        } else {
            printf("Invalid commit SHA1 format\n");
            chdir(original_cwd);
            return -1;
        }
    }

    // Compare the two commits
    gitnano_diff_result *diff;
    if ((err = gitnano_compare_snapshots(sha1, sha2, &diff)) != 0) {
        printf("ERROR: gitnano_compare_snapshots: %d\n", err);
        chdir(original_cwd);
        return err;
    }

    printf("Diff between ");
        print_colored_hash(sha1);
        printf(" and ");
        print_colored_hash(sha2);
        printf(":\n");

    if (diff->added_count > 0) {
        printf("\nAdded files (%d):\n", diff->added_count);
        for (int i = 0; i < diff->added_count; i++) {
            printf("  + %s\n", diff->added_files[i]);
        }
    }

    if (diff->modified_count > 0) {
        printf("\nModified files (%d):\n", diff->modified_count);
        for (int i = 0; i < diff->modified_count; i++) {
            printf("  M %s\n", diff->modified_files[i]);
        }
    }

    if (diff->deleted_count > 0) {
        printf("\nDeleted files (%d):\n", diff->deleted_count);
        for (int i = 0; i < diff->deleted_count; i++) {
            printf("  - %s\n", diff->deleted_files[i]);
        }
    }

    if (diff->added_count == 0 && diff->modified_count == 0 && diff->deleted_count == 0) {
        printf("\nNo differences found.\n");
    }

    gitnano_free_diff(diff);
    chdir(original_cwd);
    return 0;
}

// Auto-sync files based on diff results - used by commit
static int auto_sync_working_files() {
    // Get current working directory
    char cwd[MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("ERROR: getcwd failed\n");
        return -1;
    }

    // Simple approach: always sync all regular files from working directory to workspace
    // This ensures workspace is always up-to-date before commit
    printf("Syncing all files from working directory to workspace...\n");

    // Sync all files from working directory to workspace
    DIR *dir = opendir(".");
    if (!dir) {
        printf("ERROR: Failed to open current directory\n");
        return -1;
    }

    struct dirent *entry;
    int synced_files = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Skip ., .., and .gitnano directory
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".gitnano") == 0) {
            continue;
        }

        struct stat st;
        if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
            // It's a regular file, sync it
            if (workspace_push_file(entry->d_name) == 0) {
                synced_files++;
            }
        }
    }

    closedir(dir);
    printf("Auto-synced %d files to workspace\n", synced_files);
    return 0;
}

// Print usage
void print_usage() {
    printf("GitNano - Mini Git Implementation with Workspace Auto-Sync\n");
    printf("Usage:\n");
    printf("  gitnano init                    Initialize repository and copy all files to workspace\n");
    printf("  gitnano add <file>              Add file to staging (auto-syncs to workspace)\n");
    printf("  gitnano commit <message>        Create commit in workspace\n");
    printf("  gitnano checkout <ref> [path]   Checkout commit or restore files (auto-syncs to original)\n");
    printf("  gitnano log                     Show commit history\n");
    printf("  gitnano diff [sha1] [sha2]      Show differences between commits\n");
    printf("\nHow it works:\n");
    printf("  - All files are automatically copied to workspace on init\n");
    printf("  - 'gitnano add' auto-syncs files to workspace before staging\n");
    printf("  - 'gitnano checkout' auto-syncs restored files to original directory\n");
    printf("  - Workspace is located at: ~/GitNano/[project-name]/\n");
    printf("\nReferences can be:\n");
    printf("  - Full SHA1 (40 chars)\n");
    printf("  - Partial SHA1 (4-7 chars)\n");
    printf("  - Branch name (e.g., 'master')\n");
    printf("  - Relative reference (e.g., 'HEAD~1')\n");
    printf("\nGitNano automatically maintains file synchronization between your\n");
    printf("working directory and the isolated workspace.\n");
}

// Command handler implementations
static int handle_init(int argc, char *argv[]) {
    if (argc > 2) {
        printf("Usage: gitnano init\n");
        printf("Too many arguments: %s\n", argv[2]);
        return 1;
    }
    return gitnano_init();
}

static int handle_add(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: gitnano add <file>\n");
        return 1;
    }
    return gitnano_add(argv[2]);
}

static int handle_commit(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: gitnano commit <message>\n");
        return 1;
    }
    return gitnano_commit(argv[2]);
}

static int handle_checkout(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: gitnano checkout <reference> [path]\n");
        printf("  <reference> can be: full SHA1, partial SHA1, branch name, or HEAD~N\n");
        printf("  [path] is optional: restore specific file or directory\n");
        printf("Examples:\n");
        printf("  gitnano checkout a1b2c3d              # checkout by SHA1\n");
        printf("  gitnano checkout master               # checkout by branch\n");
        printf("  gitnano checkout HEAD~1               # checkout parent commit\n");
        printf("  gitnano checkout a1b2c3d file.txt     # restore specific file\n");
        return 1;
    }

    const char *reference = argv[2];
    const char *path = (argc >= 4) ? argv[3] : NULL;

    return gitnano_checkout(reference, path);
}

static int handle_log(int argc, char *argv[]) {
    if (argc > 2) {
        printf("Usage: gitnano log\n");
        printf("Too many arguments: %s\n", argv[2]);
        return 1;
    }
    return gitnano_log();
}

static int handle_diff(int argc, char *argv[]) {
    if (argc > 4) {
        printf("Usage: gitnano diff [sha1] [sha2]\n");
        return 1;
    }
    const char *sha1 = (argc >= 3) ? argv[2] : NULL;
    const char *sha2 = (argc == 4) ? argv[3] : NULL;
    return gitnano_diff(sha1, sha2);
}

// Helper function to collect files from a tree
static int collect_tree_files(const char *tree_sha1, file_entry **files_out) {
    int err;
    *files_out = NULL;
    tree_entry *entries = NULL;

    if ((err = tree_parse(tree_sha1, &entries)) != 0) {
        return err;
    }

    tree_entry *current = entries;
    while (current) {
        if (strcmp(current->type, "blob") == 0) {
            file_entry *entry = safe_malloc(sizeof(file_entry));
            if (!entry) {
                tree_free(entries);
                free_file_list(*files_out);
                return -1;
            }

            entry->path = safe_strdup(current->name);
            if (!entry->path) {
                free(entry);
                tree_free(entries);
                free_file_list(*files_out);
                return -1;
            }

            strncpy(entry->sha1, current->sha1, sizeof(entry->sha1) - 1);
            entry->sha1[sizeof(entry->sha1) - 1] = '\0';
            entry->next = *files_out;
            *files_out = entry;
        }
        current = current->next;
    }

    tree_free(entries);
    return 0;
}

// Helper function to find file in list
static file_entry *find_file_in_list(file_entry *list, const char *path) {
    while (list) {
        if (strcmp(list->path, path) == 0) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}


// Array of commands
const command_t commands[] = {
    {"init", handle_init},
    {"add", handle_add},
    {"commit", handle_commit},
    {"checkout", handle_checkout},
    {"log", handle_log},
    {"diff", handle_diff},
    {NULL, NULL} // Sentinel to mark the end of the array
};
