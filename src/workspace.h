#ifndef WORKSPACE_H
#define WORKSPACE_H

#include "gitnano.h"

// Workspace configuration
#define GITNANO_WORKSPACE_DIR ".gitnano/workspace"
#define WORKSPACE_BASE_DIR "~/GitNano"

// Workspace management functions
int get_workspace_name(char *workspace_name, size_t size);
int get_workspace_path(char *workspace_path, size_t size);
int get_original_path_from_workspace(const char *workspace_file_path, char *original_path, size_t size);
int get_workspace_file_path(const char *original_file_path, char *workspace_file_path, size_t size);

// Workspace initialization and synchronization
int workspace_init();
int workspace_exists();
int workspace_is_initialized();
int workspace_sync_single_file(const char *path);
int workspace_sync_from_single_file(const char *path);

// Legacy sync functions (will be deprecated)
int workspace_sync_to(const char *path);
int workspace_sync_from(const char *path);
int workspace_copy_directory(const char *src, const char *dst, const char *exclude_dir);

// File operations in workspace
int workspace_file_exists(const char *path);
char *workspace_read_file(const char *path, size_t *size);
int workspace_write_file(const char *path, const void *data, size_t size);

#endif // WORKSPACE_H