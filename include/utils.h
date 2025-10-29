#ifndef UTILS_H
#define UTILS_H

#include <openssl/evp.h>
#include <zlib.h>
#include <errno.h>
#include <dirent.h>

// Hash functions
int sha1_file(const char *path, char *sha1_out);
int sha1_data(const void *data, size_t size, char *sha1_out);

// Compression functions
int compress_data(const void *input, size_t input_size,
                  void **output, size_t *output_size);
int decompress_data(const void *input, size_t input_size,
                    void **output, size_t *output_size);

// File operations
int mkdir_p(const char *path);
int file_exists(const char *path);
char *read_file(const char *path, size_t *size);
int write_file(const char *path, const void *data, size_t size);
void get_git_timestamp(char *timestamp, size_t size);
void get_object_path(const char *sha1, char *path);

// Memory management
void *safe_malloc(size_t size);
void *safe_realloc(void *ptr, size_t size);
char *safe_strdup(const char *s);

// Extraction functions
int extract_blob(const char *sha1, const char *target_path);
int extract_tree_recursive(const char *tree_sha1, const char *base_path);
int collect_working_files(const char *dir_path, file_entry **files);
int file_in_target_tree(const char *path, file_entry *target_files);
void free_file_list(file_entry *list);
int cleanup_extra_files(const char *base_path, file_entry *target_files);
int collect_target_files(const char *tree_sha1, const char *base_path, file_entry **files);

#endif // UTILS_H