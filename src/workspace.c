#define _GNU_SOURCE
#include "workspace.h"
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>

// Expand ~ to home directory
static void expand_home_dir(const char *path, char *expanded, size_t size) {
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "";
        }
        snprintf(expanded, size, "%s%s", home, path + 1);
    } else {
        strncpy(expanded, path, size - 1);
        expanded[size - 1] = '\0';
    }
}

// Get workspace name (current directory name)
int get_workspace_name(char *workspace_name, size_t size) {
    char cwd[MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("ERROR: getcwd failed\n");
        return -1;
    }

    // Extract directory name from path
    char *last_slash = strrchr(cwd, '/');
    if (!last_slash) {
        printf("ERROR: Invalid current directory path\n");
        return -1;
    }

    const char *dir_name = last_slash + 1;
    if (strlen(dir_name) == 0) {
        printf("ERROR: Empty directory name\n");
        return -1;
    }

    strncpy(workspace_name, dir_name, size - 1);
    workspace_name[size - 1] = '\0';
    return 0;
}

// Get full workspace path
int get_workspace_path(char *workspace_path, size_t size) {
    char workspace_name[MAX_PATH];
    if (get_workspace_name(workspace_name, sizeof(workspace_name)) != 0) {
        return -1;
    }

    char base_dir[MAX_PATH];
    expand_home_dir(WORKSPACE_BASE_DIR, base_dir, sizeof(base_dir));

    snprintf(workspace_path, size, "%s/%s", base_dir, workspace_name);
    return 0;
}

// Get original file path from workspace file path
int get_original_path_from_workspace(const char *workspace_file_path, char *original_path, size_t size) {
    char workspace_base[MAX_PATH];
    if (get_workspace_path(workspace_base, sizeof(workspace_base)) != 0) {
        return -1;
    }

    // Check if the path is within workspace
    if (strncmp(workspace_file_path, workspace_base, strlen(workspace_base)) != 0) {
        printf("ERROR: Path not in workspace: %s\n", workspace_file_path);
        return -1;
    }

    // Remove workspace base path to get relative path
    const char *relative_path = workspace_file_path + strlen(workspace_base);
    if (relative_path[0] == '/') {
        relative_path++;
    }

    // Get current working directory as original base
    char cwd[MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("ERROR: getcwd failed\n");
        return -1;
    }

    snprintf(original_path, size, "%s/%s", cwd, relative_path);
    return 0;
}

// Get workspace file path from original file path
int get_workspace_file_path(const char *original_file_path, char *workspace_file_path, size_t size) {
    char workspace_base[MAX_PATH];
    if (get_workspace_path(workspace_base, sizeof(workspace_base)) != 0) {
        return -1;
    }

    // Get current working directory
    char cwd[MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("ERROR: getcwd failed\n");
        return -1;
    }

    // Convert absolute paths to relative from current directory
    char relative_path[MAX_PATH];
    if (original_file_path[0] == '/') {
        // Absolute path - convert to relative
        if (strncmp(original_file_path, cwd, strlen(cwd)) == 0) {
            const char *rel = original_file_path + strlen(cwd);
            if (rel[0] == '/') rel++;
            strcpy(relative_path, rel);
        } else {
            printf("ERROR: File path not in current directory tree: %s\n", original_file_path);
            return -1;
        }
    } else {
        // Already relative
        strcpy(relative_path, original_file_path);
    }

    snprintf(workspace_file_path, size, "%s/%s", workspace_base, relative_path);
    return 0;
}

// Check if workspace exists
int workspace_exists() {
    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        return 0;
    }
    return file_exists(workspace_path);
}

// Check if workspace has been initialized with .gitnano structure
int workspace_is_initialized() {
    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        return 0;
    }

    char gitnano_dir[MAX_PATH];
    snprintf(gitnano_dir, sizeof(gitnano_dir), "%s/.gitnano", workspace_path);

    return file_exists(gitnano_dir);
}

// Copy directory recursively, excluding specified directory
int workspace_copy_directory(const char *src, const char *dst, const char *exclude_dir) {
    DIR *dir = opendir(src);
    if (!dir) {
        printf("ERROR: Cannot open source directory: %s\n", src);
        return -1;
    }

    // Create destination directory
    if (mkdir_p(dst) != 0) {
        printf("ERROR: Cannot create destination directory: %s\n", dst);
        closedir(dir);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. and exclude directory
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            (exclude_dir && strcmp(entry->d_name, exclude_dir) == 0)) {
            continue;
        }

        char src_path[MAX_PATH];
        char dst_path[MAX_PATH];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

        struct stat st;
        if (stat(src_path, &st) != 0) {
            printf("WARNING: Cannot stat %s, skipping\n", src_path);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Recursively copy subdirectory
            if (workspace_copy_directory(src_path, dst_path, exclude_dir) != 0) {
                closedir(dir);
                return -1;
            }
        } else {
            // Copy file
            size_t size;
            char *data = read_file(src_path, &size);
            if (!data) {
                printf("WARNING: Cannot read file %s, skipping\n", src_path);
                continue;
            }

            if (write_file(dst_path, data, size) != 0) {
                printf("WARNING: Cannot write file %s, skipping\n", dst_path);
                free(data);
                continue;
            }
            free(data);
        }
    }

    closedir(dir);
    return 0;
}

// Initialize workspace - create directory structure only (lazy file population)
int workspace_init() {
    if (workspace_exists()) {
        printf("Workspace already exists\n");
        return 0;
    }

    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        return -1;
    }

    printf("Initializing workspace at: %s\n", workspace_path);

    // Create workspace directory
    if (mkdir_p(workspace_path) != 0) {
        printf("ERROR: Failed to create workspace directory: %s\n", workspace_path);
        return -1;
    }

    // Note: No longer copy all working files - workspace will be populated lazily
    // Files will be copied to workspace only when they are explicitly added

    // Initialize .gitnano directory in workspace
    char gitnano_dir[MAX_PATH];
    snprintf(gitnano_dir, sizeof(gitnano_dir), "%s/.gitnano", workspace_path);

    if (mkdir_p(gitnano_dir) != 0) {
        printf("ERROR: Failed to create .gitnano directory\n");
        return -1;
    }

    // Create .gitnano subdirectories
    char objects_dir[MAX_PATH];
    char refs_dir[MAX_PATH];
    snprintf(objects_dir, sizeof(objects_dir), "%s/objects", gitnano_dir);
    snprintf(refs_dir, sizeof(refs_dir), "%s/refs", gitnano_dir);

    if (mkdir_p(objects_dir) != 0 || mkdir_p(refs_dir) != 0) {
        printf("ERROR: Failed to create .gitnano subdirectories\n");
        return -1;
    }

    // Create initial HEAD file
    char head_file[MAX_PATH];
    snprintf(head_file, sizeof(head_file), "%s/HEAD", gitnano_dir);
    const char *head_content = "ref: refs/heads/master\n";
    if (write_file(head_file, head_content, strlen(head_content)) != 0) {
        printf("ERROR: Failed to create HEAD file\n");
        return -1;
    }

    // Create refs/heads directory
    char heads_dir[MAX_PATH];
    snprintf(heads_dir, sizeof(heads_dir), "%s/refs/heads", gitnano_dir);
    if (mkdir_p(heads_dir) != 0) {
        printf("ERROR: Failed to create refs/heads directory\n");
        return -1;
    }

    printf("Workspace initialized successfully with .gitnano structure\n");
    return 0;
}

// Sync single file to workspace (for add operations)
int workspace_sync_single_file(const char *path) {
    if (!workspace_is_initialized()) {
        printf("ERROR: Workspace not initialized. Call workspace_init() first.\n");
        return -1;
    }

    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        return -1;
    }

    char cwd[MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("ERROR: getcwd failed\n");
        return -1;
    }

    char src_path[MAX_PATH];
    char dst_path[MAX_PATH];
    snprintf(src_path, sizeof(src_path), "%s/%s", cwd, path);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", workspace_path, path);

    if (!file_exists(src_path)) {
        printf("ERROR: Source file does not exist: %s\n", src_path);
        return -1;
    }

    // Create directory if needed
    char *dir_path = safe_strdup(dst_path);
    if (dir_path) {
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkdir_p(dir_path);
        }
        free(dir_path);
    }

    // Copy the file
    size_t size;
    char *data = read_file(src_path, &size);
    if (!data) {
        printf("ERROR: Failed to read source file: %s\n", src_path);
        return -1;
    }

    int result = write_file(dst_path, data, size);
    free(data);

    if (result == 0) {
        printf("Synced %s to workspace\n", path);
    } else {
        printf("ERROR: Failed to sync file to workspace: %s\n", path);
    }

    return result;
}

// Sync files from workspace to original directory (for checkout operations)
int workspace_sync_from_single_file(const char *path) {
    if (!workspace_exists()) {
        printf("ERROR: Workspace does not exist\n");
        return -1;
    }

    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        return -1;
    }

    char cwd[MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("ERROR: getcwd failed\n");
        return -1;
    }

    char src_path[MAX_PATH];
    char dst_path[MAX_PATH];
    snprintf(src_path, sizeof(src_path), "%s/%s", workspace_path, path);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", cwd, path);

    if (!file_exists(src_path)) {
        printf("ERROR: Workspace file does not exist: %s\n", src_path);
        return -1;
    }

    // Create directory if needed
    char *dir_path = safe_strdup(dst_path);
    if (dir_path) {
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkdir_p(dir_path);
        }
        free(dir_path);
    }

    // Copy the file
    size_t size;
    char *data = read_file(src_path, &size);
    if (!data) {
        printf("ERROR: Failed to read workspace file: %s\n", src_path);
        return -1;
    }

    int result = write_file(dst_path, data, size);
    free(data);

    if (result == 0) {
        printf("Synced %s from workspace to original directory\n", path);
    } else {
        printf("ERROR: Failed to sync file from workspace: %s\n", path);
    }

    return result;
}

// Sync files to workspace (legacy function - will be removed)
int workspace_sync_to(const char *path) {
    if (!workspace_exists()) {
        printf("ERROR: Workspace does not exist. Run 'gitnano init' first.\n");
        return -1;
    }

    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        return -1;
    }

    char cwd[MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("ERROR: getcwd failed\n");
        return -1;
    }

    if (path && strlen(path) > 0) {
        // Sync specific file/directory
        char src_path[MAX_PATH];
        char dst_path[MAX_PATH];
        snprintf(src_path, sizeof(src_path), "%s/%s", cwd, path);
        get_workspace_file_path(path, dst_path, sizeof(dst_path));

        if (file_exists(src_path)) {
            struct stat st;
            if (stat(src_path, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    return workspace_copy_directory(src_path, dst_path, NULL);
                } else {
                    size_t size;
                    char *data = read_file(src_path, &size);
                    if (!data) return -1;

                    // Create directory if needed
                    char *dir_path = safe_strdup(dst_path);
                    if (dir_path) {
                        char *last_slash = strrchr(dir_path, '/');
                        if (last_slash) {
                            *last_slash = '\0';
                            mkdir_p(dir_path);
                        }
                        free(dir_path);
                    }

                    int result = write_file(dst_path, data, size);
                    free(data);
                    return result;
                }
            }
        } else {
            printf("ERROR: Source path does not exist: %s\n", src_path);
            return -1;
        }
    } else {
        // Sync entire directory
        return workspace_copy_directory(cwd, workspace_path, ".git");
    }

    return 0;
}

// Sync files from workspace
int workspace_sync_from(const char *path) {
    if (!workspace_exists()) {
        printf("ERROR: Workspace does not exist\n");
        return -1;
    }

    char workspace_path[MAX_PATH];
    if (get_workspace_path(workspace_path, sizeof(workspace_path)) != 0) {
        return -1;
    }

    char cwd[MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("ERROR: getcwd failed\n");
        return -1;
    }

    if (path && strlen(path) > 0) {
        // Sync specific file/directory from workspace
        char src_path[MAX_PATH];
        char dst_path[MAX_PATH];
        get_workspace_file_path(path, src_path, sizeof(src_path));
        snprintf(dst_path, sizeof(dst_path), "%s/%s", cwd, path);

        if (workspace_file_exists(path)) {
            struct stat st;
            if (stat(src_path, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    return workspace_copy_directory(src_path, cwd, NULL);
                } else {
                    size_t size;
                    char *data = workspace_read_file(path, &size);
                    if (!data) return -1;

                    // Create directory if needed
                    char *dir_path = safe_strdup(dst_path);
                    if (dir_path) {
                        char *last_slash = strrchr(dir_path, '/');
                        if (last_slash) {
                            *last_slash = '\0';
                            mkdir_p(dir_path);
                        }
                        free(dir_path);
                    }

                    int result = write_file(dst_path, data, size);
                    free(data);
                    return result;
                }
            }
        } else {
            printf("ERROR: Workspace path does not exist: %s\n", path);
            return -1;
        }
    } else {
        // Sync entire workspace back to original directory
        return workspace_copy_directory(workspace_path, cwd, ".gitnano");
    }

    return 0;
}

// Check if file exists in workspace
int workspace_file_exists(const char *path) {
    char workspace_file_path[MAX_PATH];
    if (get_workspace_file_path(path, workspace_file_path, sizeof(workspace_file_path)) != 0) {
        return 0;
    }
    return file_exists(workspace_file_path);
}

// Read file from workspace
char *workspace_read_file(const char *path, size_t *size) {
    char workspace_file_path[MAX_PATH];
    if (get_workspace_file_path(path, workspace_file_path, sizeof(workspace_file_path)) != 0) {
        return NULL;
    }
    return read_file(workspace_file_path, size);
}

// Write file to workspace
int workspace_write_file(const char *path, const void *data, size_t size) {
    char workspace_file_path[MAX_PATH];
    if (get_workspace_file_path(path, workspace_file_path, sizeof(workspace_file_path)) != 0) {
        return -1;
    }
    return write_file(workspace_file_path, data, size);
}