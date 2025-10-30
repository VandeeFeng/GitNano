#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h/nob.h"

typedef struct {
    const char *src_dir;
    const char *build_dir;
    const char *name;
} Build_Target;

static Build_Target build_targets[] = {
    {"src", "build", ""},
    {"src/core", "build/core", ""},
    {"src/objects", "build/objects", ""},
    {"src/utils", "build/utils", ""},
};

bool collect_source_files(Nob_File_Paths *files, const char *dir) {
    Nob_File_Paths dir_files = {0};
    if (!nob_read_entire_dir(dir, &dir_files)) return false;

    nob_da_foreach(const char*, filename, &dir_files) {
        if (nob_sv_end_with(nob_sv_from_cstr(*filename), ".c")) {
            if (strcmp(dir, "src") == 0 && strcmp(*filename, "main.c") == 0) {
                continue; // Skip main.c in src root
            }
            nob_da_append(files, nob_temp_sprintf("%s/%s", dir, *filename));
        }
    }

    nob_da_free(dir_files);
    return true;
}

bool compile_objects(Nob_Procs *procs, const char *flags[]) {
    Nob_Cmd base_cmd = {0};

    nob_cc(&base_cmd);
    nob_cc_flags(&base_cmd);
    nob_cmd_append(&base_cmd, "-std=c99", "-g", "-O2", "-Iinclude");

    if (flags) {
        for (const char **f = flags; *f; f++) {
            nob_cmd_append(&base_cmd, *f);
        }
    }

    for (size_t i = 0; i < ARRAY_LEN(build_targets); ++i) {
        Nob_File_Paths files = {0};
        if (!collect_source_files(&files, build_targets[i].src_dir)) return false;

        nob_da_foreach(const char*, filepath, &files) {
            Nob_Cmd cmd = base_cmd;

            if (strcmp(build_targets[i].src_dir, "src/utils") == 0) {
                nob_cmd_append(&cmd, "-Isrc/utils");
            }

            nob_cmd_append(&cmd, "-c", *filepath, "-o",
                           nob_temp_sprintf("build/%s.o", *filepath + 4)); // Skip "src/"

            if (!nob_cmd_run(&cmd, .async = procs)) return false;
        }

        nob_da_free(files);
    }

    return true;
}

bool link_executable(const char *output_name, const char *flags[]) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "gcc", "-o", output_name);

    Nob_File_Paths all_files = {0};
    for (size_t i = 0; i < ARRAY_LEN(build_targets); ++i) {
        if (!collect_source_files(&all_files, build_targets[i].src_dir)) return false;
    }

    nob_da_foreach(const char*, filepath, &all_files) {
        nob_cmd_append(&cmd, nob_temp_sprintf("build/%s.o", *filepath + 4));
    }

    nob_cmd_append(&cmd, "build/main.o");

    if (flags) {
        for (const char **f = flags; *f; f++) {
            nob_cmd_append(&cmd, *f);
        }
    }

    nob_cmd_append(&cmd, "-lz", "-lssl", "-lcrypto");

    bool result = nob_cmd_run(&cmd);
    nob_da_free(all_files);
    return result;
}

bool build_project(const char *flags[]) {
    // Create build directories
    for (size_t i = 0; i < ARRAY_LEN(build_targets); ++i) {
        if (!nob_mkdir_if_not_exists(build_targets[i].build_dir)) return false;
    }

    // Compile all source files
    Nob_Procs procs = {0};
    if (!compile_objects(&procs, flags)) return false;
    if (!nob_procs_flush(&procs)) return false;

    // Compile main.c
    Nob_Cmd main_cmd = {0};
    nob_cmd_append(&main_cmd, "gcc", "-Wall", "-Wextra", "-std=c99", "-g", "-O2");
    if (flags) {
        for (const char **f = flags; *f; f++) {
            nob_cmd_append(&main_cmd, *f);
        }
    }
    nob_cmd_append(&main_cmd, "-Iinclude", "-c", "src/main.c", "-o", "build/main.o");
    if (!nob_cmd_run(&main_cmd)) return false;

    return link_executable("gitnano", flags);
}

bool build_static_lib(void) {
    for (size_t i = 0; i < ARRAY_LEN(build_targets); ++i) {
        if (!nob_mkdir_if_not_exists(build_targets[i].build_dir)) return false;
    }

    Nob_Procs procs = {0};
    if (!compile_objects(&procs, NULL)) return false;
    if (!nob_procs_flush(&procs)) return false;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "ar", "rcs", "libgitnano.a");

    Nob_File_Paths all_files = {0};
    for (size_t i = 0; i < ARRAY_LEN(build_targets); ++i) {
        if (!collect_source_files(&all_files, build_targets[i].src_dir)) return false;
    }

    nob_da_foreach(const char*, filepath, &all_files) {
        nob_cmd_append(&cmd, nob_temp_sprintf("build/%s.o", *filepath + 4));
    }

    bool result = nob_cmd_run(&cmd);
    nob_da_free(all_files);
    return result;
}

bool run_tests(void) {
    if (!build_project(NULL)) return false;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "gcc", "-Wall", "-Wextra", "-std=c99", "-g", "-O2",
                   "-Iinclude", "-Isrc/utils", "-o", "test_runner", "tests/test_runner.c");

    Nob_File_Paths all_files = {0};
    for (size_t i = 0; i < ARRAY_LEN(build_targets); ++i) {
        if (!collect_source_files(&all_files, build_targets[i].src_dir)) return false;
    }

    nob_da_foreach(const char*, filepath, &all_files) {
        nob_cmd_append(&cmd, nob_temp_sprintf("build/%s.o", *filepath + 4));
    }

    nob_cmd_append(&cmd, "-lz", "-lssl", "-lcrypto");

    if (!nob_cmd_run(&cmd)) return false;

    cmd = (Nob_Cmd){0};
    nob_cmd_append(&cmd, "./test_runner");
    return nob_cmd_run(&cmd);
}

bool install_project(void) {
    if (!build_project(NULL)) return false;

    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        nob_log(NOB_ERROR, "Could not get HOME directory");
        return false;
    }

    Nob_Cmd mkdir_cmd = {0};
    nob_cmd_append(&mkdir_cmd, "mkdir", "-p", nob_temp_sprintf("%s/.local/bin", home_dir));
    if (!nob_cmd_run(&mkdir_cmd)) return false;

    Nob_Cmd install_cmd = {0};
    nob_cmd_append(&install_cmd, "cp", "gitnano", nob_temp_sprintf("%s/.local/bin/", home_dir));
    return nob_cmd_run(&install_cmd);
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program_name = nob_shift_args(&argc, &argv);

    if (argc <= 0) {
        nob_log(NOB_INFO, "Building GitNano...");
        return build_project(NULL) ? 0 : 1;
    }

    const char *command = nob_shift_args(&argc, &argv);

    if (strcmp(command, "test") == 0) {
        nob_log(NOB_INFO, "Building and running tests...");
        return run_tests() ? 0 : 1;
    }

    if (strcmp(command, "clean") == 0) {
        nob_log(NOB_INFO, "Cleaning build artifacts...");
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "rm", "-rf", "build", "gitnano", "test_runner", "libgitnano.a");
        return nob_cmd_run(&cmd) ? 0 : 1;
    }

    if (strcmp(command, "debug") == 0) {
        nob_log(NOB_INFO, "Building GitNano in debug mode...");
        const char *debug_flags[] = {"-DDEBUG", "-g3", NULL};
        return build_project(debug_flags) ? 0 : 1;
    }

    if (strcmp(command, "static-lib") == 0) {
        nob_log(NOB_INFO, "Building static library...");
        return build_static_lib() ? 0 : 1;
    }

    if (strcmp(command, "install") == 0) {
        nob_log(NOB_INFO, "Installing GitNano...");
        return install_project() ? 0 : 1;
    }

    nob_log(NOB_ERROR, "Unknown command: %s", command);
    nob_log(NOB_INFO, "Available commands:");
    nob_log(NOB_INFO, "  (no args)  - Build GitNano");
    nob_log(NOB_INFO, "  test       - Build and run tests");
    nob_log(NOB_INFO, "  clean      - Clean build artifacts");
    nob_log(NOB_INFO, "  debug      - Build with debug symbols");
    nob_log(NOB_INFO, "  static-lib - Build static library");
    nob_log(NOB_INFO, "  install    - Install to ~/.local/bin");

    return 1;
}
