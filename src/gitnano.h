#ifndef GITNANO_H
#define GITNANO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

// Object types
#define OBJ_BLOB  1
#define OBJ_TREE  2
#define OBJ_COMMIT 3

// Max path length
#define MAX_PATH 4096
#define SHA1_HEX_SIZE 41

// GitNano directory structure
#define GITNANO_DIR ".gitnano"
#define OBJECTS_DIR GITNANO_DIR "/objects"
#define REFS_DIR GITNANO_DIR "/refs"
#define HEAD_FILE GITNANO_DIR "/HEAD"
#define INDEX_FILE GITNANO_DIR "/index"

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

// Core API functions
int gitnano_init();
int gitnano_add(const char *path);
int gitnano_commit(const char *message);
int gitnano_checkout(const char *commit_sha1);
int gitnano_log();
int gitnano_diff(const char *commit1, const char *commit2);
void print_usage();

// Reference management functions (refs.c)
int get_head_ref(char *ref_out);
int set_head_ref(const char *ref);
int get_current_commit(char *sha1_out);

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
int blob_size(const char *sha1, size_t *size_out);
int blob_cat(const char *sha1, FILE *out);

// Tree functions
tree_entry *tree_entry_new(const char *mode, const char *type,
                          const char *sha1, const char *name);
int tree_build(const char *path, char *sha1_out);
int tree_parse(const char *sha1, tree_entry **entries);
int tree_write(tree_entry *entries, char *sha1_out);
void tree_free(tree_entry *entries);
tree_entry *tree_find(tree_entry *entries, const char *name);
int tree_restore(const char *tree_sha1, const char *target_dir);

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