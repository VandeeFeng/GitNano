#include "gitnano.h"
#include <pwd.h>

// Get current user information
void get_current_user(char *author, size_t size) {
    const char *username = getenv("USER");
    if (!username) {
        username = getenv("LOGNAME");
    }
    if (!username) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) {
            username = pw->pw_name;
        }
    }
    if (username) {
        strncpy(author, username, size - 1);
        author[size - 1] = '\0';
    } else {
        strncpy(author, "unknown", size - 1);
        author[size - 1] = '\0';
    }
}

// Create commit object
int commit_create(const char *tree_sha1, const char *parent_sha1,
                  const char *author, const char *message, char *sha1_out) {
    int err;
    if (!tree_sha1 || !message) return -1;

    gitnano_commit_info commit;
    strncpy(commit.tree_sha1, tree_sha1, sizeof(commit.tree_sha1) - 1);
    commit.tree_sha1[sizeof(commit.tree_sha1) - 1] = '\0';

    if (parent_sha1) {
        strncpy(commit.parent_sha1, parent_sha1, sizeof(commit.parent_sha1) - 1);
        commit.parent_sha1[sizeof(commit.parent_sha1) - 1] = '\0';
    } else {
        commit.parent_sha1[0] = '\0';
    }

    if (author) {
        strncpy(commit.author, author, sizeof(commit.author) - 1);
        commit.author[sizeof(commit.author) - 1] = '\0';
    } else {
        get_current_user(commit.author, sizeof(commit.author));
    }

    get_git_timestamp(commit.timestamp, sizeof(commit.timestamp));
    strncpy(commit.message, message, sizeof(commit.message) - 1);
    commit.message[sizeof(commit.message) - 1] = '\0';

    char commit_content[2048];
    int len = 0;
    len += sprintf(commit_content + len, "tree %s\n", commit.tree_sha1);
    if (parent_sha1) {
        len += sprintf(commit_content + len, "parent %s\n", commit.parent_sha1);
    }
    len += sprintf(commit_content + len, "author %s %s\n", commit.author, commit.timestamp);
    len += sprintf(commit_content + len, "committer %s %s\n", commit.author, commit.timestamp);
    len += sprintf(commit_content + len, "\n%s\n", commit.message);

    if ((err = object_write("commit", commit_content, len, sha1_out)) != 0) {
        printf("ERROR: object_write: %d\n", err);
        return err;
    }

    return 0;
}

// Parse commit object
int commit_parse(const char *sha1, gitnano_commit_info *commit) {
    int err;
    gitnano_object obj;
    if ((err = object_read(sha1, &obj)) != 0 || strcmp(obj.type, "commit") != 0) {
        object_free(&obj);
        printf("ERROR: object_read or type check: %d\n", err);
        return err ? err : -1;
    }

    memset(commit, 0, sizeof(gitnano_commit_info));
    const char *data = (const char *)obj.data;

    const char *tree = strstr(data, "tree ");
    if (tree) {
        tree += strlen("tree ");
        sscanf(tree, "%40s", commit->tree_sha1);
    }

    const char *parent = strstr(data, "parent ");
    if (parent) {
        parent += strlen("parent ");
        sscanf(parent, "%40s", commit->parent_sha1);
    }

    // Parse author line (format: "author <username> <timestamp>")
    const char *author = strstr(data, "author ");
    if (author) {
        author += strlen("author ");
        // Find where timestamp starts (last space in the line)
        const char *line_end = strchr(author, '\n');
        if (line_end) {
            // Work backwards to find the timestamp (starts after last space)
            const char *timestamp_start = line_end - 1;
            while (timestamp_start > author && *timestamp_start != ' ') {
                timestamp_start--;
            }
            if (timestamp_start > author) {
                timestamp_start++; // Skip the space

                // Extract timestamp
                size_t timestamp_len = line_end - timestamp_start;
                if (timestamp_len > sizeof(commit->timestamp) - 1) {
                    timestamp_len = sizeof(commit->timestamp) - 1;
                }
                strncpy(commit->timestamp, timestamp_start, timestamp_len);
                commit->timestamp[timestamp_len] = '\0';

                // Extract author (everything before timestamp)
                size_t author_len = timestamp_start - author - 1; // -1 to exclude space
                if (author_len > sizeof(commit->author) - 1) {
                    author_len = sizeof(commit->author) - 1;
                }
                strncpy(commit->author, author, author_len);
                commit->author[author_len] = '\0';
            }
        }
    }

    const char *message = strstr(data, "\n\n");
    if (message) {
        message += 2; // Skip the two newlines
        // Find the end of the message (next double newline or end of data)
        const char *message_end = strstr(message, "\n\n");
        if (!message_end) {
            // If no double newline found, go to end of data
            message_end = message + strlen(message);
        }

        // Calculate message length (excluding trailing newlines)
        size_t message_len = message_end - message;
        // Remove trailing newlines from message
        while (message_len > 0 && (message[message_len - 1] == '\n' || message[message_len - 1] == '\r')) {
            message_len--;
        }

        if (message_len > sizeof(commit->message) - 1) {
            message_len = sizeof(commit->message) - 1;
        }

        strncpy(commit->message, message, message_len);
        commit->message[message_len] = '\0';
    }

    object_free(&obj);
    return 0;
}

// Get commit tree
int commit_get_tree(const char *commit_sha1, char *tree_sha1_out) {
    int err;
    gitnano_commit_info commit;
    if ((err = commit_parse(commit_sha1, &commit)) != 0) {
        printf("ERROR: commit_parse: %d\n", err);
        return err;
    }

    strcpy(tree_sha1_out, commit.tree_sha1);
    return 0;
}

// Get commit parent
int commit_get_parent(const char *commit_sha1, char *parent_sha1_out) {
    int err;
    gitnano_commit_info commit;
    if ((err = commit_parse(commit_sha1, &commit)) != 0) {
        printf("ERROR: commit_parse: %d\n", err);
        return err;
    }

    if (strlen(commit.parent_sha1) == 0) {
        return -1; // No parent
    }

    // Verify parent commit exists in GitNano repository before returning
    if (!commit_exists(commit.parent_sha1)) {
        fprintf(stderr, "WARNING: Parent commit %s not found in GitNano repository (may be a Git commit)\n", commit.parent_sha1);
        return -1; // Parent not found in GitNano repo
    }

    strcpy(parent_sha1_out, commit.parent_sha1);
    return 0;
}

// Check if commit exists
int commit_exists(const char *sha1) {
    int err;
    char path[MAX_PATH];
    get_object_path(sha1, path);

    if (!file_exists(path)) return 0;

    gitnano_object obj;
    if ((err = object_read(sha1, &obj)) != 0) {
        return 0;
    }

    int is_commit = (strcmp(obj.type, "commit") == 0);
    object_free(&obj);

    return is_commit;
}