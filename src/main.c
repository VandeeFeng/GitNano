
#include "gitnano.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[1], commands[i].name) == 0) {
            return commands[i].handler(argc, argv);
        }
    }

    printf("Unknown command: %s\n", argv[1]);
    print_usage();
    return 1;
}
