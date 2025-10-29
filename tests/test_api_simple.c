#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "src/gitnano.h"

int main() {
    printf("=== GitNano API Test ===\n\n");

    // Change to test directory
    chdir("/tmp/test_gitnano");

    // Test repository status
    gitnano_status_info status;
    if (gitnano_status(&status) == 0) {
        printf("Repository Status:\n");
        printf("  Is repository: %s\n", status.is_repo ? "Yes" : "No");
        printf("  Has commits: %s\n", status.has_commits ? "Yes" : "No");
        printf("  Current commit: %s\n", status.current_commit);
        printf("  Current branch: %s\n", status.current_branch);
        printf("  Staged files: %d\n", status.staged_files);
    } else {
        printf("Failed to get repository status\n");
    }

    printf("\n=== API Test Complete ===\n");
    return 0;
}