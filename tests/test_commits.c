#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "gitnano.h"

int main() {
    printf("=== GitNano Commit Test ===\n");

    // Change to tests directory
    chdir("tests");

    // Initialize repository if needed
    if (gitnano_init() != 0) {
        printf("Repository already exists or init failed, continuing...\n");
    }

    // Add test.txt file
    printf("Adding test.txt...\n");
    if (gitnano_add("test.txt") != 0) {
        printf("FAIL: Could not add test.txt\n");
        return 1;
    }
    printf("PASS: test.txt added\n");

    // First commit
    printf("Making first commit...\n");
    if (gitnano_commit("First test commit") != 0) {
        printf("FAIL: First commit failed\n");
        return 1;
    }
    printf("PASS: First commit successful\n");

    // Modify test.txt for second commit
    FILE *f = fopen("test.txt", "a");
    if (f) {
        fprintf(f, "\nModified content for second commit.");
        fclose(f);
    }

    // Add modified file
    printf("Adding modified test.txt...\n");
    if (gitnano_add("test.txt") != 0) {
        printf("FAIL: Could not add modified test.txt\n");
        return 1;
    }
    printf("PASS: Modified test.txt added\n");

    // Second commit - this tests the SHA1_HEX_SIZE fix
    printf("Making second commit...\n");
    if (gitnano_commit("Second test commit") != 0) {
        printf("FAIL: Second commit failed - SHA1_HEX_SIZE issue detected!\n");
        return 1;
    }
    printf("PASS: Second commit successful - SHA1_HEX_SIZE fix working!\n");

    // Third commit to be sure
    printf("Making third commit...\n");
    if (gitnano_commit("Third test commit") != 0) {
        printf("FAIL: Third commit failed\n");
        return 1;
    }
    printf("PASS: Third commit successful\n");

    printf("\n=== All commit tests passed! ===\n");
    printf("SHA1_HEX_SIZE fix verified successfully.\n");

    return 0;
}