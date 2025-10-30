#ifndef DIFF_H
#define DIFF_H

#include "gitnano.h"

// Diff-related functions
int is_safe_filename(const char *filename);
int safe_file_compare(const char *file1, const char *file2);
int collect_working_changes(file_entry *commit_files, int *added_count, int *modified_count, int *deleted_count);
void display_diff_summary(int added_count, int modified_count, int deleted_count, file_entry *commit_files);
int diff_working_directory(const char *commit_sha1);
int compare_commits(const char *sha1, const char *sha2);

// Helper functions needed by diff operations
int collect_tree_files(const char *tree_sha1, file_entry **files_out);
file_entry *find_file_in_list(file_entry *list, const char *path);

#endif // DIFF_H