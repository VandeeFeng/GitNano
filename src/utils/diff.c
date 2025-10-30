#include "diff.h"
#include "gitnano.h"
#include "memory.h"
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <strings.h>
#include <ctype.h>

// Check if filename is safe for diff operations
int is_safe_filename(const char *filename) {
    if (!filename || strlen(filename) == 0) {
        return 0;
    }

    // Check length
    if (strlen(filename) > 255) {
        return 0;
    }

    // Skip hidden files and system files
    if (filename[0] == '.') {
        return 0;
    }

    // Check for safe characters only (alphanumeric, underscore, hyphen, dot)
    for (int i = 0; filename[i]; i++) {
        char c = filename[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
            return 0;
        }
    }

    // Skip files that are unlikely to be version controlled
    const char *skip_extensions[] = {
        ".o", ".obj", ".exe", ".dll", ".so", ".dylib",  // compiled binaries
        ".tmp", ".temp", ".swp", ".swo",                 // temporary files
        ".log", ".out",                                  // log/output files
        ".pid", ".lock",                                 // system files
        ".bak", ".backup",                               // backup files
        ".cache", ".tmp",                                // cache files
        ".DS_Store",                                     // macOS system files
        ".Thumbs.db",                                    // Windows thumbnail cache
        NULL
    };

    const char *ext = strrchr(filename, '.');
    if (ext) {
        for (int i = 0; skip_extensions[i]; i++) {
            if (strcasecmp(ext, skip_extensions[i]) == 0) {
                return 0;
            }
        }
    }

    // Skip common temporary file patterns
    if (strstr(filename, "tmp") || strstr(filename, "temp") ||
        strstr(filename, "cache") || strstr(filename, "lock") ||
        strstr(filename, "backup")) {
        return 0;
    }

    return 1;
}

// Safe file comparison using exec() instead of system()
int safe_file_compare(const char *file1, const char *file2) {
    pid_t pid;
    int status;
    int pipe_fd[2];
    char result_buf;

    // Create pipe to capture diff result
    if (pipe(pipe_fd) == -1) {
        return -1;
    }

    pid = fork();
    if (pid == -1) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(pipe_fd[0]);  // Close read end

        // Redirect stdout to pipe
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);

        // Execute diff command
        execlp("diff", "diff", "-q", file1, file2, NULL);

        // If execlp fails
        _exit(1);
    } else {
        // Parent process
        close(pipe_fd[1]);  // Close write end

        // Read result (just one character to check if there's output)
        read(pipe_fd[0], &result_buf, 1);  // We don't need the actual result, just check if read succeeds
        close(pipe_fd[0]);

        // Wait for child to complete
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            // diff returns 0 if files are identical, 1 if different, 2 if error
            return exit_code;
        }

        return -1;
    }
}

// Helper function to collect working directory changes
int collect_working_changes(file_entry *commit_files, int *added_count, int *modified_count, int *deleted_count) {
    *added_count = *modified_count = *deleted_count = 0;

    // Check working directory files
    DIR *dir = opendir(".");
    if (!dir) return 0;

    struct dirent *entry;
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
            file_entry *commit_file = find_file_in_list(commit_files, entry->d_name);
            if (!commit_file) {
                (*added_count)++;
            } else {
                // Check if modified using safe file comparison
                char workspace_path[MAX_PATH];
                get_workspace_path(workspace_path, sizeof(workspace_path));
                char *workspace_file = safe_asprintf("%s/%s", workspace_path, entry->d_name);

                int result = safe_file_compare(entry->d_name, workspace_file);
                if (result == 1) {  // Files are different
                    (*modified_count)++;
                }
                free(workspace_file);
            }
        }
    }
    closedir(dir);

    // Check for deleted files (only safe files)
    file_entry *c = commit_files;
    while (c) {
        if (is_safe_filename(c->path)) {
            struct stat st;
            if (stat(c->path, &st) != 0) {
                (*deleted_count)++;
            }
        }
        c = c->next;
    }

    return 0;
}

// Helper function to display diff results
void display_diff_summary(int added_count, int modified_count, int deleted_count, file_entry *commit_files) {
    if (added_count > 0) {
        printf("\nAdded files (%d):\n", added_count);
        DIR *dir = opendir(".");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 ||
                    strcmp(entry->d_name, "..") == 0 ||
                    strcmp(entry->d_name, ".gitnano") == 0 ||
                    !is_safe_filename(entry->d_name)) {
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
                    strcmp(entry->d_name, ".gitnano") == 0 ||
                    !is_safe_filename(entry->d_name)) {
                    continue;
                }
                struct stat st;
                if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                    file_entry *commit_file = find_file_in_list(commit_files, entry->d_name);
                    if (commit_file) {
                        char workspace_path[MAX_PATH];
                        get_workspace_path(workspace_path, sizeof(workspace_path));
                        char *workspace_file = safe_asprintf("%s/%s", workspace_path, entry->d_name);

                        int result = safe_file_compare(entry->d_name, workspace_file);
                        if (result == 1) {  // Files are different
                            printf("  M %s\n", entry->d_name);
                        }
                        free(workspace_file);
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
            if (is_safe_filename(c->path)) {
                struct stat st;
                if (stat(c->path, &st) != 0) {
                    printf("  - %s\n", c->path);
                }
            }
            c = c->next;
        }
    }

    if (added_count == 0 && modified_count == 0 && deleted_count == 0) {
        printf("\nNo changes in working directory.\n");
    }
}

// Helper function to diff working directory with commit
int diff_working_directory(const char *commit_sha1) {
    int err;
    char commit_tree_sha1[SHA1_HEX_SIZE];
    char workspace_path[MAX_PATH];
    char original_cwd[MAX_PATH];

    // Get workspace path and save current directory
    get_workspace_path(workspace_path, sizeof(workspace_path));
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        printf("ERROR: Failed to get current directory\n");
        return -1;
    }

    // Change to workspace to read commit objects
    if (chdir(workspace_path) != 0) {
        printf("ERROR: Failed to change to workspace directory\n");
        return -1;
    }

    if ((err = commit_get_tree(commit_sha1, commit_tree_sha1)) != 0) {
        printf("ERROR: Failed to get tree from current commit: %d\n", err);
        chdir(original_cwd);
        return err;
    }

    printf("Working directory changes:\n");

    // Get current commit tree files (still in workspace)
    file_entry *commit_files = NULL;
    if (collect_tree_files(commit_tree_sha1, &commit_files) != 0) {
        printf("ERROR: Failed to get current commit files\n");
        chdir(original_cwd);
        return -1;
    }

    // Change back to original directory to check working directory files
    if (chdir(original_cwd) != 0) {
        printf("ERROR: Failed to change back to original directory\n");
        free_file_list(commit_files);
        return -1;
    }

    // Collect and display changes
    int added_count = 0, modified_count = 0, deleted_count = 0;
    collect_working_changes(commit_files, &added_count, &modified_count, &deleted_count);
    display_diff_summary(added_count, modified_count, deleted_count, commit_files);

    free_file_list(commit_files);
    return 0;
}

// Helper function to compare two commits
int compare_commits(const char *sha1, const char *sha2) {
    int err;
    gitnano_diff_result *diff;

    if ((err = gitnano_compare_snapshots(sha1, sha2, &diff)) != 0) {
        printf("ERROR: gitnano_compare_snapshots: %d\n", err);
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
            if (is_safe_filename(diff->added_files[i])) {
                printf("  + %s\n", diff->added_files[i]);
            }
        }
    }

    if (diff->modified_count > 0) {
        printf("\nModified files (%d):\n", diff->modified_count);
        for (int i = 0; i < diff->modified_count; i++) {
            if (is_safe_filename(diff->modified_files[i])) {
                printf("  M %s\n", diff->modified_files[i]);
            }
        }
    }

    if (diff->deleted_count > 0) {
        printf("\nDeleted files (%d):\n", diff->deleted_count);
        for (int i = 0; i < diff->deleted_count; i++) {
            if (is_safe_filename(diff->deleted_files[i])) {
                printf("  - %s\n", diff->deleted_files[i]);
            }
        }
    }

    if (diff->added_count == 0 && diff->modified_count == 0 && diff->deleted_count == 0) {
        printf("\nNo differences found.\n");
    }

    gitnano_free_diff(diff);
    return 0;
}

// Helper function to collect files from a tree
int collect_tree_files(const char *tree_sha1, file_entry **files_out) {
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
file_entry *find_file_in_list(file_entry *list, const char *path) {
    while (list) {
        if (strcmp(list->path, path) == 0) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}