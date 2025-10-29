#include "gitnano.h"
#include <pwd.h>

// Get current user information
void get_current_user(char *author, size_t size) {
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        snprintf(author, size, "%s <%s@%s>", pw->pw_gecos ? pw->pw_gecos : pw->pw_name,
                 pw->pw_name, "localhost");
    } else {
        snprintf(author, size, "unknown <unknown@localhost>");
    }
}

// Create commit object
int commit_create(const char *tree_sha1, const char *parent_sha1,
                  const char *author, const char *message, char *sha1_out) {
    if (!tree_sha1 || !message) return -1;

    // Build commit content
    size_t commit_size = 0;
    char *commit_data = NULL;

    // Calculate size
    commit_size += strlen("tree ") + strlen(tree_sha1) + 1; // "tree <sha1>\n"

    if (parent_sha1) {
        commit_size += strlen("parent ") + strlen(parent_sha1) + 1;
    }

    char timestamp[32];
    get_git_timestamp(timestamp, sizeof(timestamp));

    char full_author[512];
    if (author) {
        strncpy(full_author, author, sizeof(full_author) - 1);
    } else {
        get_current_user(full_author, sizeof(full_author));
    }

    commit_size += strlen("author ") + strlen(full_author) + 1 + strlen(timestamp) + 1;
    commit_size += strlen("committer ") + strlen(full_author) + 1 + strlen(timestamp) + 2; // extra \n
    commit_size += 1 + strlen(message) + 1; // \n + message + \n

    // Allocate memory
    commit_data = malloc(commit_size);
    if (!commit_data) return -1;

    // Build commit content
    char *ptr = commit_data;

    // Tree line
    ptr += sprintf(ptr, "tree %s\n", tree_sha1);

    // Parent line (optional)
    if (parent_sha1) {
        ptr += sprintf(ptr, "parent %s\n", parent_sha1);
    }

    // Author line
    ptr += sprintf(ptr, "author %s %s\n", full_author, timestamp);

    // Committer line
    ptr += sprintf(ptr, "committer %s %s\n", full_author, timestamp);

    // Empty line and message
    ptr += sprintf(ptr, "\n%s\n", message);

    // Write commit object
    int result = object_write("commit", commit_data, strlen(commit_data), sha1_out);

    free(commit_data);
    return result;
}

// Parse commit object
int commit_parse(const char *sha1, gitnano_commit_info *commit) {
    gitnano_object obj;

    if (object_read(sha1, &obj) != 0) {
        return -1;
    }

    if (strcmp(obj.type, "commit") != 0) {
        object_free(&obj);
        return -1;
    }

    char *content = obj.data;
    char *line_end;

    // Initialize commit structure
    memset(commit, 0, sizeof(gitnano_commit_info));

    // Parse tree line
    if (strncmp(content, "tree ", 5) == 0) {
        line_end = strchr(content + 5, '\n');
        if (line_end) {
            size_t len = line_end - (content + 5);
            if (len < SHA1_HEX_SIZE - 1) {
                strncpy(commit->tree_sha1, content + 5, len);
                commit->tree_sha1[len] = '\0';
            }
            content = line_end + 1;
        }
    }

    // Parse parent line (optional)
    if (strncmp(content, "parent ", 7) == 0) {
        line_end = strchr(content + 7, '\n');
        if (line_end) {
            size_t len = line_end - (content + 7);
            if (len < SHA1_HEX_SIZE - 1) {
                strncpy(commit->parent_sha1, content + 7, len);
                commit->parent_sha1[len] = '\0';
            }
            content = line_end + 1;
        }
    }

    // Parse author line
    if (strncmp(content, "author ", 7) == 0) {
        line_end = strchr(content + 7, '\n');
        if (line_end) {
            size_t len = line_end - (content + 7);
            if (len < sizeof(commit->author) - 1) {
                strncpy(commit->author, content + 7, len);
                commit->author[len] = '\0';
            }
            content = line_end + 1;
        }
    }

    // Parse committer line
    if (strncmp(content, "committer ", 10) == 0) {
        line_end = strchr(content + 10, '\n');
        if (line_end) {
            // Extract timestamp from committer line
            char *space = strrchr(content + 10, ' ');
            if (space) {
                size_t len = space - (content + 10);
                if (len < sizeof(commit->timestamp) - 1) {
                    strncpy(commit->timestamp, content + 10, len);
                    commit->timestamp[len] = '\0';
                }
            }
            content = line_end + 1;
        }
    }

    // Skip empty line
    if (*content == '\n') {
        content++;
    }

    // Parse message
    line_end = strchr(content, '\n');
    if (line_end) {
        size_t len = line_end - content;
        if (len < sizeof(commit->message) - 1) {
            strncpy(commit->message, content, len);
            commit->message[len] = '\0';
        }
    } else {
        strncpy(commit->message, content, sizeof(commit->message) - 1);
        commit->message[sizeof(commit->message) - 1] = '\0';
    }

    object_free(&obj);
    return 0;
}

// Get commit tree
int commit_get_tree(const char *commit_sha1, char *tree_sha1_out) {
    gitnano_commit_info commit;
    if (commit_parse(commit_sha1, &commit) != 0) {
        return -1;
    }

    strcpy(tree_sha1_out, commit.tree_sha1);
    return 0;
}

// Get commit parent
int commit_get_parent(const char *commit_sha1, char *parent_sha1_out) {
    gitnano_commit_info commit;
    if (commit_parse(commit_sha1, &commit) != 0) {
        return -1;
    }

    if (strlen(commit.parent_sha1) == 0) {
        return -1; // No parent
    }

    strcpy(parent_sha1_out, commit.parent_sha1);
    return 0;
}

// Check if commit exists
int commit_exists(const char *sha1) {
    char path[MAX_PATH];
    get_object_path(sha1, path);

    if (!file_exists(path)) return 0;

    gitnano_object obj;
    if (object_read(sha1, &obj) != 0) return 0;

    int is_commit = (strcmp(obj.type, "commit") == 0);
    object_free(&obj);

    return is_commit;
}