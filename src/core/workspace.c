#define _GNU_SOURCE
#include "../../include/workspace.h"
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

    char *gitnano_dir = safe_asprintf("%s/.gitnano", workspace_path);

    int result = file_exists(gitnano_dir);
    free(gitnano_dir);
    return result;
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
    char *gitnano_dir = safe_asprintf("%s/.gitnano", workspace_path);

    if (mkdir_p(gitnano_dir) != 0) {
        printf("ERROR: Failed to create .gitnano directory\n");
        return -1;
    }

    // Create .gitnano subdirectories
    char *objects_dir = safe_asprintf("%s/objects", gitnano_dir);

    char *refs_dir = safe_asprintf("%s/refs", gitnano_dir);

    if (mkdir_p(objects_dir) != 0 || mkdir_p(refs_dir) != 0) {
        printf("ERROR: Failed to create .gitnano subdirectories\n");
        return -1;
    }

    // Create initial HEAD file
    char *head_file = safe_asprintf("%s/HEAD", gitnano_dir);
    const char *head_content = "ref: refs/heads/master\n";
    if (write_file(head_file, head_content, strlen(head_content)) != 0) {
        printf("ERROR: Failed to create HEAD file\n");
        return -1;
    }

    // Create refs/heads directory
    char *heads_dir = safe_asprintf("%s/refs/heads", gitnano_dir);
    if (mkdir_p(heads_dir) != 0) {
        printf("ERROR: Failed to create refs/heads directory\n");
        free(gitnano_dir);
        free(objects_dir);
        free(refs_dir);
        free(head_file);
        free(heads_dir);
        return -1;
    }

    free(gitnano_dir);
    free(objects_dir);
    free(refs_dir);
    free(head_file);
    free(heads_dir);

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

    char *src_path = safe_asprintf("%s/%s", cwd, path);

    char *dst_path = safe_asprintf("%s/%s", workspace_path, path);

    if (!file_exists(src_path)) {
        printf("ERROR: Source file does not exist: %s\n", src_path);
        free(src_path);
        free(dst_path);
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
        free(src_path);
        free(dst_path);
        return -1;
    }

    int result = write_file(dst_path, data, size);
    free(data);

    if (result == 0) {
        printf("Synced %s to workspace\n", path);
    } else {
        printf("ERROR: Failed to sync file to workspace: %s\n", path);
    }

    free(src_path);
    free(dst_path);

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

    char *src_path = safe_asprintf("%s/%s", workspace_path, path);

    char *dst_path = safe_asprintf("%s/%s", cwd, path);

    if (!file_exists(src_path)) {
        printf("ERROR: Workspace file does not exist: %s\n", src_path);
        free(src_path);
        free(dst_path);
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
        free(src_path);
        free(dst_path);
        return -1;
    }

    int result = write_file(dst_path, data, size);
    free(data);

    if (result == 0) {
        printf("Synced %s from workspace to original directory\n", path);
    } else {
        printf("ERROR: Failed to sync file from workspace: %s\n", path);
    }

    free(src_path);
    free(dst_path);

    return result;
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