#include "gitnano.h"

static int check_repo_exists() {
    if (!file_exists(GITNANO_DIR)) {
        printf("Not a GitNano repository\n");
        return -1;
    }
    return 0;
}

// Initialize GitNano repository
int gitnano_init() {
    int err;
    if (file_exists(GITNANO_DIR)) {
        printf("GitNano repository already exists\n");
        return 0;
    }

    // Create directories
    if ((err = mkdir_p(GITNANO_DIR)) != 0) {
        printf("ERROR: mkdir_p .gitnano: %d\n", err);
        return err;
    }

    if ((err = mkdir_p(OBJECTS_DIR)) != 0) {
        printf("ERROR: mkdir_p objects: %d\n", err);
        return err;
    }

    if ((err = mkdir_p(REFS_DIR)) != 0) {
        printf("ERROR: mkdir_p refs: %d\n", err);
        return err;
    }

    // Create initial HEAD file pointing to master
    const char *head_content = "ref: refs/heads/master\n";
    if ((err = write_file(HEAD_FILE, head_content, strlen(head_content))) != 0) {
        printf("ERROR: write_file HEAD: %d\n", err);
        return err;
    }

    // Create master branch reference
    if ((err = mkdir_p(REFS_DIR "/heads")) != 0) {
        printf("ERROR: mkdir_p refs/heads: %d\n", err);
        return err;
    }

    printf("Initialized empty GitNano repository\n");
    return 0;
}

// Add file to staging area
int gitnano_add(const char *path) {
    int err;
    if (check_repo_exists() != 0) return -1;

    if (!file_exists(path)) {
        printf("File not found: %s\n", path);
        return -1;
    }

    // Read file content
    size_t size;
    char *data = read_file(path, &size);
    if (!data) {
        printf("Failed to read file: %s\n", path);
        return -1;
    }

    // Create blob
    char sha1[SHA1_HEX_SIZE];
    if ((err = blob_write(data, size, sha1)) != 0) {
        free(data);
        printf("ERROR: blob_write: %d\n", err);
        return err;
    }

    free(data);

    // Update index (simplified - just append)
    FILE *index_fp = fopen(INDEX_FILE, "a");
    if (!index_fp) {
        // If append fails, try to create the file
        index_fp = fopen(INDEX_FILE, "w");
        if (!index_fp) {
            printf("ERROR: fopen index: %d\n", -1);
            return -1;
        }
    }

    fprintf(index_fp, "%s %s\n", sha1, path);
    fclose(index_fp);

    printf("Added %s (blob: %s)\n", path, sha1);
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

    // Build tree from current directory
    char tree_sha1[SHA1_HEX_SIZE];
    if ((err = tree_build(".", tree_sha1)) != 0) {
        printf("ERROR: tree_build: %d\n", err);
        return err;
    }

    // Get parent commit
    char parent_sha1[SHA1_HEX_SIZE] = {0};
    get_current_commit(parent_sha1);

    // Create commit
    char commit_sha1[SHA1_HEX_SIZE];
    if ((err = commit_create(tree_sha1, strlen(parent_sha1) > 0 ? parent_sha1 : NULL,
                      NULL, message, commit_sha1)) != 0) {
        printf("ERROR: commit_create: %d\n", err);
        return err;
    }

    // Update HEAD
    char ref[MAX_PATH];
    if ((err = get_head_ref(ref)) != 0) {
        printf("ERROR: get_head_ref: %d\n", err);
        return err;
    }

    if (strncmp(ref, "refs/heads/", 11) == 0) {
        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", GITNANO_DIR, ref);

        char branch_content[SHA1_HEX_SIZE + 2];
        snprintf(branch_content, sizeof(branch_content), "%s\n", commit_sha1);
        if ((err = write_file(full_path, branch_content, strlen(branch_content))) != 0) {
            printf("ERROR: write_file: %d\n", err);
            return err;
        }
    } else {
        if ((err = set_head_ref(commit_sha1)) != 0) {
            printf("ERROR: set_head_ref: %d\n", err);
            return err;
        }
    }

    printf("Committed %s\n", commit_sha1);
    return 0;
}

// Checkout commit
int gitnano_checkout(const char *commit_sha1) {
    int err;
    if (check_repo_exists() != 0) return -1;

    if (!commit_exists(commit_sha1)) {
        printf("Commit not found: %s\n", commit_sha1);
        return -1;
    }

    // Get tree from commit
    char tree_sha1[SHA1_HEX_SIZE];
    if ((err = commit_get_tree(commit_sha1, tree_sha1)) != 0) {
        printf("ERROR: commit_get_tree: %d\n", err);
        return err;
    }

    // Restore tree to current directory
    if ((err = tree_restore(tree_sha1, ".")) != 0) {
        printf("ERROR: tree_restore: %d\n", err);
        return err;
    }

    // Update HEAD to point to the checked out commit
    if ((err = set_head_ref(commit_sha1)) != 0) {
        printf("ERROR: set_head_ref: %d\n", err);
        // This is not a fatal error, so we can continue
    }

    printf("Checked out commit %s\n", commit_sha1);
    return 0;
}

// Show commit log
int gitnano_log() {
    int err;
    if (check_repo_exists() != 0) return -1;

    char current_sha1[SHA1_HEX_SIZE];
    if ((err = get_current_commit(current_sha1)) != 0) {
        printf("No commits found\n");
        return 0; // Not an error
    }

    printf("Commit history:\n");

    while (strlen(current_sha1) > 0) {
        gitnano_commit_info commit;
        if ((err = commit_parse(current_sha1, &commit)) != 0) {
            printf("ERROR: commit_parse: %d\n", err);
            break;
        }

        printf("\ncommit %s\n", current_sha1);
        printf("Author: %s\n", commit.author);
        printf("Date:   %s\n", commit.timestamp);
        printf("\n    %s\n", commit.message);

        // Move to parent
        if (commit_get_parent(current_sha1, current_sha1) != 0) {
            break;
        }
    }

    return 0;
}

// Show diff between commits
int gitnano_diff(const char *commit1, const char *commit2) {
    int err;
    if (check_repo_exists() != 0) return -1;

    char sha1[SHA1_HEX_SIZE] = {0};
    char sha2[SHA1_HEX_SIZE] = {0};

    // Handle different argument scenarios
    if (!commit1 && !commit2) {
        // gitnano diff: compare working directory with current commit
        if (get_current_commit(sha1) != 0) {
            printf("No commits found to compare\n");
            return -1;
        }
        printf("Comparing working directory with commit %s\n", sha1);
        printf("Working directory diff not yet implemented\n");
        return 0;
    } else if (commit1 && !commit2) {
        // gitnano diff <sha1>: compare with current commit
        if (get_current_commit(sha2) != 0) {
            printf("No current commit found\n");
            return -1;
        }
        if (strlen(commit1) == SHA1_HEX_SIZE - 1) {
            strcpy(sha1, commit1);
        } else {
            printf("Invalid commit SHA1: %s\n", commit1);
            return -1;
        }
    } else if (commit1 && commit2) {
        // gitnano diff <sha1> <sha2>: compare two commits
        if (strlen(commit1) == SHA1_HEX_SIZE - 1 && strlen(commit2) == SHA1_HEX_SIZE - 1) {
            strcpy(sha1, commit1);
            strcpy(sha2, commit2);
        } else {
            printf("Invalid commit SHA1 format\n");
            return -1;
        }
    }

    // Compare the two commits
    gitnano_diff_result *diff;
    if ((err = gitnano_compare_snapshots(sha1, sha2, &diff)) != 0) {
        printf("ERROR: gitnano_compare_snapshots: %d\n", err);
        return err;
    }

    // Display diff results
    printf("Diff between %s and %s:\n", sha1, sha2);

    if (diff->added_count > 0) {
        printf("\nAdded files (%d):\n", diff->added_count);
        for (int i = 0; i < diff->added_count; i++) {
            printf("  + %s\n", diff->added_files[i]);
        }
    }

    if (diff->modified_count > 0) {
        printf("\nModified files (%d):\n", diff->modified_count);
        for (int i = 0; i < diff->modified_count; i++) {
            printf("  M %s\n", diff->modified_files[i]);
        }
    }

    if (diff->deleted_count > 0) {
        printf("\nDeleted files (%d):\n", diff->deleted_count);
        for (int i = 0; i < diff->deleted_count; i++) {
            printf("  - %s\n", diff->deleted_files[i]);
        }
    }

    if (diff->added_count == 0 && diff->modified_count == 0 && diff->deleted_count == 0) {
        printf("\nNo differences found.\n");
    }

    gitnano_free_diff(diff);
    return 0;
}

// Print usage
void print_usage() {
    printf("GitNano - Mini Git Implementation\n");
    printf("Usage:\n");
    printf("  gitnano init              Initialize repository\n");
    printf("  gitnano add <file>        Add file to staging\n");
    printf("  gitnano commit <message>  Create commit\n");
    printf("  gitnano checkout <sha1>   Checkout commit\n");
    printf("  gitnano log               Show commit history\n");
    printf("  gitnano diff [sha1] [sha2] Show differences between commits\n");
}

// Command handler implementations
static int handle_init(int argc, char *argv[]) {
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
        printf("Usage: gitnano checkout <sha1>\n");
        return 1;
    }
    return gitnano_checkout(argv[2]);
}

static int handle_log(int argc, char *argv[]) {
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

// Array of commands
const command_t commands[] = {
    {"init", handle_init},
    {"add", handle_add},
    {"commit", handle_commit},
    {"checkout", handle_checkout},
    {"log", handle_log},
    {"diff", handle_diff},
    {NULL, NULL} // Sentinel to mark the end of the array
};