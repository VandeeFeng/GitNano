#include "gitnano.h"

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
        // Direct SHA-1 reference
        strcpy(content, ref);
    } else {
        // Branch reference
        snprintf(content, sizeof(content), "ref: %s", ref);
    }
    strcat(content, "\n");

    if ((err = write_file(HEAD_FILE, content, strlen(content))) != 0) {
        printf("ERROR: write_file: %d\n", err);
        return err;
    }

    return 0;
}

// Get current branch or commit SHA-1
int get_current_commit(char *sha1_out) {
    int err;
    char ref[MAX_PATH];
    if ((err = get_head_ref(ref)) != 0) {
        printf("ERROR: get_head_ref: %d\n", err);
        return err;
    }

    if (strncmp(ref, "refs/heads/", 11) == 0) {
        // Construct full path to branch file
        char full_path[MAX_PATH];
        if (strlen(GITNANO_DIR) + 1 + strlen(ref) < MAX_PATH) {
            strcpy(full_path, GITNANO_DIR);
            strcat(full_path, "/");
            strcat(full_path, ref);
        } else {
            printf("ERROR: Path too long\n");
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
            printf("ERROR: read_file: %d\n", -1);
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
            printf("ERROR: Invalid SHA1 format\n");
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
            printf("ERROR: Invalid SHA1 format\n");
            sha1_out[0] = '\0';
            return -1;
        }
    }
}