#include "gitnano.h"

// Initialize GitNano repository
int gitnano_init() {
    if (file_exists(GITNANO_DIR)) {
        printf("GitNano repository already exists\n");
        return 0;
    }

    // Create directories
    if (mkdir_p(GITNANO_DIR) != 0) {
        perror("Failed to create .gitnano directory");
        return -1;
    }

    if (mkdir_p(OBJECTS_DIR) != 0) {
        perror("Failed to create objects directory");
        return -1;
    }

    if (mkdir_p(REFS_DIR) != 0) {
        perror("Failed to create refs directory");
        return -1;
    }

    // Create initial HEAD file pointing to master
    const char *head_content = "ref: refs/heads/master\n";
    if (write_file(HEAD_FILE, head_content, strlen(head_content)) != 0) {
        perror("Failed to create HEAD file");
        return -1;
    }

    // Create master branch reference
    if (mkdir_p(REFS_DIR "/heads") != 0) {
        perror("Failed to create heads directory");
        return -1;
    }

    printf("Initialized empty GitNano repository\n");
    return 0;
}

// Get current HEAD reference
int get_head_ref(char *ref_out) {
    if (!file_exists(HEAD_FILE)) {
        return -1;
    }

    size_t size;
    char *content = read_file(HEAD_FILE, &size);
    if (!content) return -1;

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
    char content[MAX_PATH];
    if (strlen(ref) == SHA1_HEX_SIZE - 1) {
        // Direct SHA-1 reference
        strcpy(content, ref);
    } else {
        // Branch reference
        snprintf(content, sizeof(content), "ref: %s", ref);
    }
    strcat(content, "\n");

    return write_file(HEAD_FILE, content, strlen(content));
}

// Get current branch or commit SHA-1
int get_current_commit(char *sha1_out) {
    char ref[MAX_PATH];
    if (get_head_ref(ref) != 0) {
        return -1;
    }

    if (strncmp(ref, "refs/heads/", 11) == 0) {
        // Construct full path to branch file
        char full_path[MAX_PATH];
        if (strlen(GITNANO_DIR) + 1 + strlen(ref) < MAX_PATH) {
            strcpy(full_path, GITNANO_DIR);
            strcat(full_path, "/");
            strcat(full_path, ref);
        } else {
            printf("Error: Path too long\n");
            return -1;
        }

        // Branch reference - read from branch file
        if (!file_exists(full_path)) {
            // Branch doesn't exist yet, might be a new repository
            sha1_out[0] = '\0';
            return -1;
        }

        size_t size;
        char *content = read_file(full_path, &size);
        if (!content) {
            sha1_out[0] = '\0';
            return -1;
        }

        char *newline = strchr(content, '\n');
        if (newline) {
            *newline = '\0';
        }

        // Validate SHA1 format
        if (strlen(content) == SHA1_HEX_SIZE - 1) {
            strcpy(sha1_out, content);
            free(content);
            return 0;
        } else {
            free(content);
            sha1_out[0] = '\0';
            return -1;
        }
    } else {
        // Direct SHA-1 reference
        if (strlen(ref) == SHA1_HEX_SIZE - 1) {
            strcpy(sha1_out, ref);
            return 0;
        } else {
            sha1_out[0] = '\0';
            return -1;
        }
    }
}

// Add file to staging area
int gitnano_add(const char *path) {
    if (!file_exists(GITNANO_DIR)) {
        printf("GitNano repository not found. Run 'gitnano init' first.\n");
        return -1;
    }

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
    if (blob_write(data, size, sha1) != 0) {
        free(data);
        printf("Failed to create blob for: %s\n", path);
        return -1;
    }

    free(data);

    // Update index (simplified - just append)
    FILE *index_fp = fopen(INDEX_FILE, "a");
    if (!index_fp) {
        index_fp = fopen(INDEX_FILE, "w");
    }

    if (index_fp) {
        fprintf(index_fp, "%s %s\n", sha1, path);
        fclose(index_fp);
    }

    printf("Added %s (blob: %s)\n", path, sha1);
    return 0;
}

// Create commit
int gitnano_commit(const char *message) {
    if (!file_exists(GITNANO_DIR)) {
        printf("GitNano repository not found. Run 'gitnano init' first.\n");
        return -1;
    }

    if (!message || strlen(message) == 0) {
        printf("Commit message cannot be empty\n");
        return -1;
    }

    // Build tree from current directory
    char tree_sha1[SHA1_HEX_SIZE];
    if (tree_build(".", tree_sha1) != 0) {
        printf("Failed to build tree from current directory\n");
        return -1;
    }

    // Get parent commit
    char parent_sha1[SHA1_HEX_SIZE] = {0};
    get_current_commit(parent_sha1);

    // Create commit
    char commit_sha1[SHA1_HEX_SIZE];
    if (commit_create(tree_sha1, strlen(parent_sha1) > 0 ? parent_sha1 : NULL,
                      NULL, message, commit_sha1) != 0) {
        printf("Failed to create commit\n");
        return -1;
    }

    // Update HEAD
    char ref[MAX_PATH];
    if (get_head_ref(ref) != 0) {
        // Error reading HEAD
        return -1;
    }

    if (strncmp(ref, "refs/heads/", 11) == 0) {
        // Construct full path to branch file
        char full_path[MAX_PATH];
        if (strlen(GITNANO_DIR) + 1 + strlen(ref) < MAX_PATH) {
            strcpy(full_path, GITNANO_DIR);
            strcat(full_path, "/");
            strcat(full_path, ref);
        } else {
            printf("Error: Path too long\n");
            return -1;
        }

        // Branch reference - check if branch file exists
        if (file_exists(full_path)) {
            // Update existing branch file with commit SHA1 and newline
            char branch_content[SHA1_HEX_SIZE + 2];
            snprintf(branch_content, sizeof(branch_content), "%s\n", commit_sha1);
            write_file(full_path, branch_content, strlen(branch_content));
        } else {
            // First commit on this branch - create branch file
            char branch_content[SHA1_HEX_SIZE + 2];
            snprintf(branch_content, sizeof(branch_content), "%s\n", commit_sha1);
            write_file(full_path, branch_content, strlen(branch_content));
        }
    } else {
        // Direct SHA1 reference - this shouldn't happen in normal workflow
        // Convert to branch reference
        char master_ref[] = "refs/heads/master";
        char branch_content[SHA1_HEX_SIZE + 2];
        snprintf(branch_content, sizeof(branch_content), "%s\n", commit_sha1);
        write_file(master_ref, branch_content, strlen(branch_content));
        set_head_ref(master_ref);
    }

    printf("Committed %s\n", commit_sha1);
    return 0;
}

// Checkout commit
int gitnano_checkout(const char *commit_sha1) {
    if (!file_exists(GITNANO_DIR)) {
        printf("GitNano repository not found. Run 'gitnano init' first.\n");
        return -1;
    }

    if (!commit_exists(commit_sha1)) {
        printf("Commit not found: %s\n", commit_sha1);
        return -1;
    }

    // Get tree from commit
    char tree_sha1[SHA1_HEX_SIZE];
    if (commit_get_tree(commit_sha1, tree_sha1) != 0) {
        printf("Failed to get tree from commit\n");
        return -1;
    }

    // Restore tree to current directory
    if (tree_restore(tree_sha1, ".") != 0) {
        printf("Failed to restore tree from commit %s\n", commit_sha1);
        return -1;
    }

    // Update HEAD to point to the checked out commit
    if (set_head_ref(commit_sha1) != 0) {
        printf("Warning: Failed to update HEAD reference\n");
    }

    printf("Checked out commit %s\n", commit_sha1);
    return 0;
}

// Show commit log
int gitnano_log() {
    if (!file_exists(GITNANO_DIR)) {
        printf("GitNano repository not found. Run 'gitnano init' first.\n");
        return -1;
    }

    char current_sha1[SHA1_HEX_SIZE];
    if (get_current_commit(current_sha1) != 0) {
        printf("No commits found\n");
        return 0;
    }

    printf("Commit history:\n");

    while (strlen(current_sha1) > 0) {
        gitnano_commit_info commit;
        if (commit_parse(current_sha1, &commit) != 0) {
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
    if (!file_exists(GITNANO_DIR)) {
        printf("GitNano repository not found. Run 'gitnano init' first.\n");
        return -1;
    }

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
        return 0; // TODO: Implement working directory comparison
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
    if (gitnano_compare_snapshots(sha1, sha2, &diff) != 0) {
        printf("Failed to compare commits\n");
        return -1;
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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "init") == 0) {
        return gitnano_init();
    } else if (strcmp(argv[1], "add") == 0) {
        if (argc < 3) {
            printf("Usage: gitnano add <file>\n");
            return 1;
        }
        return gitnano_add(argv[2]);
    } else if (strcmp(argv[1], "commit") == 0) {
        if (argc < 3) {
            printf("Usage: gitnano commit <message>\n");
            return 1;
        }
        return gitnano_commit(argv[2]);
    } else if (strcmp(argv[1], "checkout") == 0) {
        if (argc < 3) {
            printf("Usage: gitnano checkout <sha1>\n");
            return 1;
        }
        return gitnano_checkout(argv[2]);
    } else if (strcmp(argv[1], "log") == 0) {
        return gitnano_log();
    } else if (strcmp(argv[1], "diff") == 0) {
        if (argc == 2) {
            return gitnano_diff(NULL, NULL);
        } else if (argc == 3) {
            return gitnano_diff(argv[2], NULL);
        } else if (argc == 4) {
            return gitnano_diff(argv[2], argv[3]);
        } else {
            printf("Usage: gitnano diff [sha1] [sha2]\n");
            return 1;
        }
    } else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
        return 1;
    }
}