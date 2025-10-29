#ifndef GITNANO_H
#define GITNANO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>

// Object types
#define OBJ_BLOB  1
#define OBJ_TREE  2
#define OBJ_COMMIT 3

// Max path length
#define MAX_PATH 8192
#define SHA1_HEX_SIZE 41

// GitNano directory structure
#define GITNANO_DIR ".gitnano"
#define OBJECTS_DIR GITNANO_DIR "/objects"
#define REFS_DIR GITNANO_DIR "/refs"
#define HEAD_FILE GITNANO_DIR "/HEAD"
#define INDEX_FILE GITNANO_DIR "/index"

// Command structure for main.c dispatch
typedef int (*command_handler_t)(int argc, char *argv[]);
typedef struct {
    const char *name;
    command_handler_t handler;
} command_t;

extern const command_t commands[];

// Object structure
typedef struct {
    char type[10];
    size_t size;
    void *data;
} gitnano_object;

// Tree entry structure
typedef struct tree_entry {
    char mode[8];
    char type[10];
    char sha1[SHA1_HEX_SIZE];
    char name[MAX_PATH];
    struct tree_entry *next;
} tree_entry;

// Commit structure
typedef struct {
    char tree_sha1[SHA1_HEX_SIZE];
    char parent_sha1[SHA1_HEX_SIZE];
    char author[256];
    char timestamp[32];
    char message[1024];
} gitnano_commit_info;

// Snapshot information for API
typedef struct {
    char id[SHA1_HEX_SIZE];
    char message[1024];
    char author[256];
    char timestamp[32];
    char tree_hash[SHA1_HEX_SIZE];
} gitnano_snapshot_info;

// File entry structure for diff operations
typedef struct file_entry {
    char *path;
    char sha1[SHA1_HEX_SIZE];
    struct file_entry *next;
} file_entry;

// Diff result structure
typedef struct {
    char **added_files;
    char **modified_files;
    char **deleted_files;
    int added_count;
    int modified_count;
    int deleted_count;
} gitnano_diff_result;

// Repository status
typedef struct {
    int is_repo;
    int has_commits;
    char current_commit[SHA1_HEX_SIZE];
    char current_branch[256];
    int staged_files;
} gitnano_status_info;

// Checkout operation statistics
typedef struct {
    int modified_count;
    int added_count;
    int deleted_count;
    char **modified_files;
    char **added_files;
    char **deleted_files;
} checkout_operation_stats;

// Core API functions
int gitnano_init();
int gitnano_add(const char *path);
int gitnano_commit(const char *message);
int gitnano_checkout(const char *reference, const char *path);
int gitnano_log();
int gitnano_diff(const char *commit1, const char *commit2);
int gitnano_sync(const char *direction, const char *path);
void print_usage();

// Reference management functions (refs.c)
int get_head_ref(char *ref_out);
int set_head_ref(const char *ref);
int get_current_commit(char *sha1_out);
int resolve_reference(const char *reference, char *sha1_out);
void print_colored_hash(const char *sha1);

// Object storage functions
int object_write(const char *type, const void *data, size_t size, char *sha1_out);
int object_read(const char *sha1, gitnano_object *obj);
int object_hash(const char *type, const void *data, size_t size, char *sha1_out);
void object_free(gitnano_object *obj);

// Blob functions
int blob_write(const char *data, size_t size, char *sha1_out);
int blob_read(const char *sha1, char **data, size_t *size);
int blob_create_from_file(const char *filepath, char *sha1_out);
int blob_exists(const char *sha1);

// Tree functions
tree_entry *tree_entry_new(const char *mode, const char *type,
                           const char *sha1, const char *name);
void tree_entry_add(tree_entry **entries, tree_entry *new_entry);
int tree_build(const char *path, char *sha1_out);
int tree_parse(const char *sha1, tree_entry **entries);
int tree_write(tree_entry *entries, char *sha1_out);
void tree_free(tree_entry *entries);
tree_entry *tree_find(tree_entry *entries, const char *name);
int tree_restore(const char *tree_sha1, const char *target_dir);
int tree_restore_path(const char *tree_sha1, const char *tree_path, const char *target_path);
void free_checkout_stats(checkout_operation_stats *stats);
void print_checkout_summary(const checkout_operation_stats *stats);

// Commit functions
void get_current_user(char *author, size_t size);
int commit_create(const char *tree_sha1, const char *parent_sha1,
                  const char *author, const char *message, char *sha1_out);
int commit_parse(const char *sha1, gitnano_commit_info *commit);
int commit_get_tree(const char *commit_sha1, char *tree_sha1_out);
int commit_get_parent(const char *commit_sha1, char *parent_sha1_out);
int commit_exists(const char *sha1);

// Utility functions
int sha1_file(const char *path, char *sha1_out);
int sha1_data(const void *data, size_t size, char *sha1_out);
int compress_data(const void *input, size_t input_size,
                  void **output, size_t *output_size);
int decompress_data(const void *input, size_t input_size,
                    void **output, size_t *output_size);
int mkdir_p(const char *path);
int file_exists(const char *path);
char *read_file(const char *path, size_t *size);
int write_file(const char *path, const void *data, size_t size);
void get_git_timestamp(char *timestamp, size_t size);
void get_object_path(const char *sha1, char *path);

// Safe memory allocation helper functions
void *safe_malloc(size_t size);
void *safe_realloc(void *ptr, size_t size);
char *safe_strdup(const char *s);
char *safe_asprintf(const char *fmt, ...);

// File system operations (moved from tree.c)
int extract_blob(const char *sha1, const char *target_path);
int extract_tree_recursive(const char *tree_sha1, const char *base_path);
int collect_working_files(const char *dir_path, file_entry **files);
int file_in_target_tree(const char *path, file_entry *target_files);
void free_file_list(file_entry *list);
int cleanup_extra_files(const char *base_path, file_entry *target_files);
int collect_target_files(const char *tree_sha1, const char *base_path, file_entry **files);

// Workspace management functions
int get_workspace_name(char *workspace_name, size_t size);
int get_workspace_path(char *workspace_path, size_t size);
int get_original_path_from_workspace(const char *workspace_file_path, char *original_path, size_t size);
int get_workspace_file_path(const char *original_file_path, char *workspace_file_path, size_t size);
int workspace_init();
int workspace_exists();
int workspace_is_initialized();
int workspace_push_file(const char *path);
int workspace_pullback_file(const char *path);
int workspace_sync_all_from_workspace();
int workspace_file_exists(const char *path);
char *workspace_read_file(const char *path, size_t *size);
int workspace_write_file(const char *path, const void *data, size_t size);

// High-level API functions
int gitnano_create_snapshot(const char *message, char *snapshot_id);
int gitnano_list_snapshots(gitnano_snapshot_info **snapshots, int *count);
int gitnano_restore_snapshot(const char *snapshot_id);
int gitnano_get_file_at_snapshot(const char *snapshot_id, const char *file_path,
                                 char **content, size_t *size);
int gitnano_compare_snapshots(const char *snapshot1, const char *snapshot2,
                              gitnano_diff_result **diff);
void gitnano_free_diff(gitnano_diff_result *diff);
int gitnano_status(gitnano_status_info *status);
void gitnano_cleanup();

#endif // GITNANO_H
