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
static const char* find_commit_field(const char* commit_data, const char* field_name) {
    const char* field = strstr(commit_data, field_name);
    if (field) {
        return field + strlen(field_name);
    }
    return NULL;
}

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

    const char *tree = find_commit_field(data, "tree ");
    if (tree) sscanf(tree, "%40s", commit->tree_sha1);

    const char *parent = find_commit_field(data, "parent ");
    if (parent) sscanf(parent, "%40s", commit->parent_sha1);

    const char *author = find_commit_field(data, "author ");
    if (author) {
        const char *email_start = strchr(author, '<');
        if (email_start) {
            strncpy(commit->author, author, email_start - author - 1);
            commit->author[sizeof(commit->author) - 1] = '\0';
        }
    }

    const char *committer = find_commit_field(data, "committer ");
    if (committer) {
        const char *timestamp_start = strrchr(committer, ' ');
        if (timestamp_start) {
            sscanf(timestamp_start + 1, "%s", commit->timestamp);
        }
    }

    const char *message = strstr(data, "\n\n");
    if (message) {
        strncpy(commit->message, message + 2, sizeof(commit->message) - 1);
        commit->message[sizeof(commit->message) - 1] = '\0';
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