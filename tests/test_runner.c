#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "src/gitnano.h"

// Simple test framework
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s\n", message); \
            return 0; \
        } else { \
            printf("PASS: %s\n", message); \
        } \
    } while(0)

int test_blob_operations() {
    printf("\n=== Testing Blob Operations ===\n");

    // Create a temporary directory for testing
    mkdir("/tmp/gitnano_test", 0755);
    chdir("/tmp/gitnano_test");

    // Initialize repository
    TEST_ASSERT(gitnano_init() == 0, "Repository initialization");

    // Create test file
    const char* test_content = "Hello GitNano test!";
    FILE* test_file = fopen("test_file.txt", "w");
    TEST_ASSERT(test_file != NULL, "Create test file");
    fprintf(test_file, "%s", test_content);
    fclose(test_file);

    // Add file
    TEST_ASSERT(gitnano_add("test_file.txt") == 0, "Add file to repository");

    // Commit
    TEST_ASSERT(gitnano_commit("Test commit") == 0, "Create commit");

    // Cleanup
    chdir("/home/vandee/Vandee/Projects/GitNano");
    system("rm -rf /tmp/gitnano_test");

    return 1;
}

int main() {
    printf("=== GitNano Test Suite ===\n");

    int passed = 0;
    int total = 0;

    total++;
    if (test_blob_operations()) {
        passed++;
    }

    printf("\n=== Test Results: %d/%d passed ===\n", passed, total);

    if (passed == total) {
        printf("All tests passed!\n");
        return 0;
    } else {
        printf("Some tests failed!\n");
        return 1;
    }
}