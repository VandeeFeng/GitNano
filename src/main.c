#include "gitnano.h"

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