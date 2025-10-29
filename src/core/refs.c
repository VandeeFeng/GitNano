#include "gitnano.h"
#include <dirent.h>

// Helper function to find object by partial SHA1
static int find_object_by_partial_sha1(const char *partial_sha1, char *full_sha1) {
    if (!partial_sha1 || !full_sha1 || strlen(partial_sha1) < 4) return -1;

    char objects_dir[MAX_PATH];
    snprintf(objects_dir, sizeof(objects_dir), "%s", OBJECTS_DIR);

    DIR *dir = opendir(objects_dir);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strlen(entry->d_name) != 2) continue;

        char *subdir = safe_asprintf("%s/%s", objects_dir, entry->d_name);

        DIR *subdir_ptr = opendir(subdir);
        if (!subdir_ptr) {
            free(subdir);
            continue;
        }

        struct dirent *obj_entry;
        while ((obj_entry = readdir(subdir_ptr)) != NULL) {
            // Skip . and .. entries
            if (strcmp(obj_entry->d_name, ".") == 0 || strcmp(obj_entry->d_name, "..") == 0) continue;

            // Construct full SHA1
            char *candidate_sha1 = safe_asprintf("%s%s", entry->d_name, obj_entry->d_name);

            // Check if candidate SHA1 starts with partial_sha1 (proper prefix matching)
            if (strncmp(candidate_sha1, partial_sha1, strlen(partial_sha1)) == 0) {
                // Verify this is a valid commit object
                if (commit_exists(candidate_sha1)) {
                    strcpy(full_sha1, candidate_sha1);
                    free(candidate_sha1);
                    free(subdir);
                    closedir(subdir_ptr);
                    closedir(dir);
                    return 0;
                }
            }
            free(candidate_sha1);
        }
        free(subdir);
        closedir(subdir_ptr);
    }

    closedir(dir);
    return -1;
}

// Resolve reference to full SHA1
int resolve_reference(const char *reference, char *sha1_out) {
    if (!reference || !sha1_out) {
        fprintf(stderr, "ERROR: resolve_reference: invalid arguments\n");
        return -1;
    }

    // Handle HEAD references first (before partial SHA1 matching)
    if (strncmp(reference, "HEAD", 4) == 0) {
        char current_sha1[SHA1_HEX_SIZE];
        int result = get_current_commit(current_sha1);
        if (result == 0) {
            if (strcmp(reference, "HEAD") == 0) {
                strcpy(sha1_out, current_sha1);
                return 0;
            }

            // Parse HEAD~N
            if (reference[4] == '~' && reference[5] >= '1' && reference[5] <= '9') {
                int n = atoi(reference + 5);
                strcpy(sha1_out, current_sha1);

                for (int i = 0; i < n && strlen(sha1_out) > 0; i++) {
                    if (commit_get_parent(sha1_out, sha1_out) != 0) {
                        fprintf(stderr, "ERROR: resolve_reference: failed to get parent commit %d for HEAD~%d\n", i+1, n);
                        return -1;
                    }
                }

                if (strlen(sha1_out) > 0) {
                    return 0;
                } else {
                    fprintf(stderr, "ERROR: resolve_reference: HEAD~%d goes beyond commit history\n", n);
                    return -1;
                }
            }
        } else {
            fprintf(stderr, "ERROR: resolve_reference: failed to get current commit for HEAD reference\n");
            return -1;
        }
    }

    // Check if it's a full SHA1
    if (strlen(reference) == SHA1_HEX_SIZE - 1) {
        if (commit_exists(reference)) {
            strcpy(sha1_out, reference);
            return 0;
        }
        fprintf(stderr, "ERROR: resolve_reference: commit not found for SHA1 %s\n", reference);
        return -1;
    }

    // Check if it's a partial SHA1 (4-8 characters) - before branch names
    if (strlen(reference) >= 4 && strlen(reference) <= 8) {
        int result = find_object_by_partial_sha1(reference, sha1_out);
        if (result == 0) {
            return 0;
        }
        // Don't return error here - continue to check branch names
    }

    // Check if it's a branch name
    if (strncmp(reference, "refs/heads/", 11) != 0) {
        char *branch_ref = safe_asprintf("refs/heads/%s", reference);

        char *full_path = safe_asprintf("%s/%s", GITNANO_DIR, branch_ref);

        if (file_exists(full_path)) {
            size_t size;
            char *content = read_file(full_path, &size);
            if (content) {
                char *newline = strchr(content, '\n');
                if (newline) *newline = '\0';

                if (strlen(content) == SHA1_HEX_SIZE - 1 && commit_exists(content)) {
                    strcpy(sha1_out, content);
                    free(content);
                    free(branch_ref);
                    free(full_path);
                    return 0;
                }
                fprintf(stderr, "ERROR: resolve_reference: invalid commit SHA1 in branch %s: %s\n", reference, content);
                free(content);
                free(branch_ref);
                free(full_path);
                return -1;
            } else {
                fprintf(stderr, "ERROR: resolve_reference: failed to read branch file %s\n", full_path);
                free(branch_ref);
                free(full_path);
                return -1;
            }
        }
        free(branch_ref);
        free(full_path);
    } else {
        // Full reference path provided
        char *full_path = safe_asprintf("%s/%s", GITNANO_DIR, reference);

        if (file_exists(full_path)) {
            size_t size;
            char *content = read_file(full_path, &size);
            if (content) {
                char *newline = strchr(content, '\n');
                if (newline) *newline = '\0';

                if (strlen(content) == SHA1_HEX_SIZE - 1 && commit_exists(content)) {
                    strcpy(sha1_out, content);
                    free(content);
                    free(full_path);
                    return 0;
                }
                fprintf(stderr, "ERROR: resolve_reference: invalid commit SHA1 in reference %s: %s\n", reference, content);
                free(content);
                free(full_path);
                return -1;
            } else {
                fprintf(stderr, "ERROR: resolve_reference: failed to read reference file %s\n", full_path);
                free(full_path);
                return -1;
            }
        } else {
            fprintf(stderr, "ERROR: resolve_reference: reference %s not found\n", reference);
            free(full_path);
            return -1;
        }
    }

    // If we got here and it's a partial SHA1, that means the partial SHA1 check failed
    if (strlen(reference) >= 4 && strlen(reference) <= 8) {
        fprintf(stderr, "ERROR: resolve_reference: no commit found matching partial SHA1 '%s'\n", reference);
        return -1;
    }

    // If it's not a full reference path and not a partial SHA1, it's likely a branch name that doesn't exist
    if (strncmp(reference, "refs/heads/", 11) != 0 && !(strlen(reference) >= 4 && strlen(reference) <= 8)) {
        fprintf(stderr, "ERROR: resolve_reference: branch '%s' not found\n", reference);
        return -1;
    }

    fprintf(stderr, "ERROR: resolve_reference: cannot resolve reference '%s'\n", reference);
    return -1;
}

// Get current HEAD reference
int get_head_ref(char *ref_out) {
    if (!file_exists(HEAD_FILE)) {
        return -1;
    }

    size_t size;
    char *content = read_file(HEAD_FILE, &size);
    if (!content) {
        printf("ERROR: read_file: %d\n", -1);
        return -1;
    }

    // Parse "ref: refs/heads/master"
    if (strncmp(content, "ref: ", 5) == 0) {
        char *newline = strchr(content + 5, '\n');
        if (newline) {
            *newline = '\0';
        }
        strcpy(ref_out, content + 5);
        free(content);
        return 0;
    }

    // Direct SHA-1 reference
    char *newline = strchr(content, '\n');
    if (newline) {
        *newline = '\0';
    }
    strcpy(ref_out, content);
    free(content);
    return 0;
}

// Set HEAD reference
int set_head_ref(const char *ref) {
    int err;
    char content[MAX_PATH];
    if (strlen(ref) == SHA1_HEX_SIZE - 1) {
        strcpy(content, ref);
    } else {
        snprintf(content, sizeof(content), "ref: %s", ref);
    }
    strcat(content, "\n");

    if ((err = write_file(HEAD_FILE, content, strlen(content))) != 0) {
        printf("ERROR: write_file: %d\n", err);
        return err;
    }

    return 0;
}

// Helper function to construct full path
static void construct_full_path(const char *ref, char *full_path) {
    snprintf(full_path, MAX_PATH, "%s/%s", GITNANO_DIR, ref);
}

// Helper function to read and validate SHA1 from file
static int read_sha1_from_file(const char *path, char *sha1_out) {
    size_t size;
    char *content = read_file(path, &size);
    if (!content) return -1;

    char *newline = strchr(content, '\n');
    if (newline) *newline = '\0';

    int result = (strlen(content) == SHA1_HEX_SIZE - 1) ? 0 : -1;
    if (result == 0) {
        strcpy(sha1_out, content);
    }

    free(content);
    return result;
}

// Get current branch or commit SHA-1
int get_current_commit(char *sha1_out) {
    if (!sha1_out) {
        fprintf(stderr, "ERROR: get_current_commit: invalid output buffer\n");
        return -1;
    }

    char ref[MAX_PATH];
    int result = get_head_ref(ref);
    if (result != 0) {
        fprintf(stderr, "ERROR: get_current_commit: failed to get HEAD reference (code %d)\n", result);
        return -1;
    }

    if (strncmp(ref, "refs/heads/", 11) == 0) {
        // Branch reference
        char full_path[MAX_PATH];
        construct_full_path(ref, full_path);

        if (!file_exists(full_path)) {
            sha1_out[0] = '\0';
            return -1;
        }

        result = read_sha1_from_file(full_path, sha1_out);
        if (result != 0) {
            fprintf(stderr, "ERROR: get_current_commit: failed to read SHA1 from branch file: %s\n", full_path);
        }
        return result;
    } else {
        // Direct SHA-1 reference
        if (strlen(ref) == SHA1_HEX_SIZE - 1) {
            if (commit_exists(ref)) {
                strcpy(sha1_out, ref);
                return 0;
            } else {
                fprintf(stderr, "ERROR: get_current_commit: commit object not found: %s\n", ref);
                return -1;
            }
        } else {
            fprintf(stderr, "ERROR: get_current_commit: invalid SHA1 format in HEAD: %s\n", ref);
            sha1_out[0] = '\0';
            return -1;
        }
    }
}
