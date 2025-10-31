#define _GNU_SOURCE
#include "gitnano.h"
#include "diff.h"
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <strings.h>

static int check_repo_exists();
static int auto_sync_working_files();

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


    if (path && strlen(path) > 0) {
        // Path checkout - restore specific file/directory in workspace
        printf("Restoring '%s' from %s...\n", path, reference);

        char tree_sha1[SHA1_HEX_SIZE];
        if ((err = commit_get_tree(commit_sha1, tree_sha1)) != 0) {
            printf("ERROR: commit_get_tree: %d\n", err);
            chdir(original_cwd);
            return err;
        }

        if ((err = tree_restore_path(tree_sha1, path, path)) != 0) {
            printf("ERROR: tree_restore_path: %d\n", err);
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
            chdir(original_cwd);
            return err;
        }

        if ((err = tree_restore(tree_sha1, ".")) != 0) {
            printf("ERROR: tree_restore: %d\n", err);
            chdir(original_cwd);
            return err;
        }

        // Update HEAD to point to the checked out commit
        if ((err = set_head_ref(commit_sha1)) != 0) {
            printf("ERROR: set_head_ref: %d\n", err);
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
        char formatted_date[32];
        format_git_timestamp(commit.timestamp, formatted_date, sizeof(formatted_date));
        printf("Date: %s\n", formatted_date);
        printf("Commit message: %s\n", commit.message);

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
    char workspace_path[MAX_PATH];
    char original_cwd[MAX_PATH];
    char sha1[SHA1_HEX_SIZE] = {0};
    char sha2[SHA1_HEX_SIZE] = {0};

    // Basic validation
    if (check_repo_exists() != 0) return -1;

    // Setup workspace paths
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        printf("ERROR: Failed to get workspace path\n");
        return -1;
    }

    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        printf("ERROR: Failed to get current directory\n");
        return -1;
    }

    // Change to workspace directory for gitnano operations
    if (chdir(workspace_path) != 0) {
        printf("ERROR: Failed to change to workspace directory\n");
        return -1;
    }

    // Handle different argument scenarios
    if (!commit1 && !commit2) {
        // Diff working directory with current commit
        if (get_current_commit(sha1) != 0) {
            printf("No commits found to compare\n");
            chdir(original_cwd);
            return -1;
        }

        printf("Comparing working directory with commit ");
        print_colored_hash(sha1);
        printf("\n");

        // Change back to original directory for working directory comparison
        if (chdir(original_cwd) != 0) {
            printf("ERROR: Failed to change back to original directory\n");
            return -1;
        }

        return diff_working_directory(sha1);
    }

    // Handle commit-to-commit diff scenarios
    if (commit1 && !commit2) {
        // Diff specified commit with current commit
        if (get_current_commit(sha2) != 0) {
            printf("No current commit found\n");
            chdir(original_cwd);
            return -1;
        }

        if (strlen(commit1) != SHA1_HEX_SIZE - 1) {
            printf("Invalid commit SHA1: %s\n", commit1);
            chdir(original_cwd);
            return -1;
        }
        strcpy(sha1, commit1);
    } else if (commit1 && commit2) {
        // Diff two specified commits
        if (strlen(commit1) != SHA1_HEX_SIZE - 1 || strlen(commit2) != SHA1_HEX_SIZE - 1) {
            printf("Invalid commit SHA1 format\n");
            chdir(original_cwd);
            return -1;
        }
        strcpy(sha1, commit1);
        strcpy(sha2, commit2);
    }

    // Compare two commits using helper function
    int result = compare_commits(sha1, sha2);
    chdir(original_cwd);
    return result;
}

// Status command - shows current directory status and sync status
int gitnano_status() {
    char cwd[MAX_PATH];
    char workspace_path[MAX_PATH];

    if (!getcwd(cwd, sizeof(cwd))) {
        printf("ERROR: Failed to get current directory\n");
        return -1;
    }

    printf("GitNano Status\n");
    printf("==============\n");
    printf("Current directory: %s\n", cwd);

    // Try to find if there's a GitNano workspace for this directory
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        printf("No GitNano workspace found for this directory\n");
        return 0;
    }

    printf("Workspace: %s\n", workspace_path);

    // Check if workspace exists and is initialized
    if (!workspace_exists()) {
        printf("Workspace does not exist. Run 'gitnano init' in a GitNano repository first.\n");
        return 0;
    }

    if (!workspace_is_initialized()) {
        printf("Workspace exists but not initialized with .gitnano structure\n");
        return 0;
    }

    printf("\nFile synchronization status:\n");

    // Get current commit files from workspace for comparison
    char *gitnano_dir = safe_asprintf("%s/.gitnano", workspace_path);

    file_entry *workspace_files = NULL;
    int has_commits = 0;

    // Change to workspace to get current commit files if there are commits
    if (file_exists(gitnano_dir)) {
        if (chdir(workspace_path) == 0) {
            char current_sha1[SHA1_HEX_SIZE];
            if (get_current_commit(current_sha1) == 0) {
                has_commits = 1;
                char tree_sha1[SHA1_HEX_SIZE];
                if (commit_get_tree(current_sha1, tree_sha1) == 0) {
                    collect_tree_files(tree_sha1, &workspace_files);
                }
            }
            chdir(cwd);  // Change back to original directory
        }
    }

    int added_count = 0, modified_count = 0, deleted_count = 0;

    if (has_commits && workspace_files) {
        collect_working_changes(workspace_files, &added_count, &modified_count, &deleted_count);

        display_diff_summary(added_count, modified_count, deleted_count, workspace_files);
    } else {
        printf("\nNo commits found. All files are new:\n");

        collect_working_changes(NULL, &added_count, &modified_count, &deleted_count);

        display_diff_summary(added_count, modified_count, deleted_count, NULL);
    }

    // Calculate total files and summary
    int total_files = added_count + modified_count;
    int unsynced_files = added_count + modified_count + deleted_count;

    printf("\nSummary:\n");
    printf("  Total files: %d\n", total_files);
    printf("  Synced files: %d\n", total_files - unsynced_files);
    printf("  Unsynced files: %d\n", unsynced_files);

    if (unsynced_files > 0) {
        printf("\nWarning: %d file(s) have changes not synchronized to workspace\n", unsynced_files);
        printf("Run 'gitnano add <file>' to sync specific files\n");
        printf("Run 'gitnano commit <message>' to sync all files and create commit\n");
    } else {
        printf("\nAll files are synchronized with workspace\n");
    }

    // Show GitNano repository status if workspace has .gitnano
    if (file_exists(gitnano_dir)) {
        printf("\nGitNano repository status:\n");

        // Change to workspace directory to check repository status
        if (chdir(workspace_path) == 0) {
            char current_sha1[SHA1_HEX_SIZE];
            if (get_current_commit(current_sha1) == 0) {
                printf("  Current commit: ");
                print_colored_hash(current_sha1);
                printf("\n");

                char ref[MAX_PATH];
                if (get_head_ref(ref) == 0 && strncmp(ref, "refs/heads/", 11) == 0) {
                    printf("  Current branch: %s\n", ref + 11);
                }
            } else {
                printf("  No commits found\n");
            }

            // Change back to original directory
            chdir(cwd);
        }
    }

    // Clean up
    free(gitnano_dir);
    if (workspace_files) {
        free_file_list(workspace_files);
    }

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
        // Skip ., .., .gitnano directory and unsafe files
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".gitnano") == 0 ||
            !is_safe_filename(entry->d_name)) {
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
    printf("  gitnano status                  Show current directory and workspace sync status\n");
    printf("\nHow it works:\n");
    printf("  - All files are automatically copied to workspace on init\n");
    printf("  - 'gitnano add' auto-syncs files to workspace before staging\n");
    printf("  - 'gitnano checkout' auto-syncs restored files to original directory\n");
    printf("  - 'gitnano status' shows sync status between working directory and workspace\n");
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

static int handle_status(int argc, char *argv[]) {
    if (argc > 2) {
        printf("Usage: gitnano status\n");
        printf("Too many arguments: %s\n", argv[2]);
        return 1;
    }
    return gitnano_status();
}

// Array of commands
const command_t commands[] = {
    {"init", handle_init},
    {"add", handle_add},
    {"commit", handle_commit},
    {"checkout", handle_checkout},
    {"log", handle_log},
    {"diff", handle_diff},
    {"status", handle_status},
    {NULL, NULL} // Sentinel to mark the end of the array
};
