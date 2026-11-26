#define _GNU_SOURCE

#define NOB_IMPLEMENTATION
#define NOB_WARN_DEPRECATED
#define NOB_EXPERIMENTAL_DELETE_OLD

#include <nob.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

#define ANSI_RED    "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_RESET  "\033[0m"

static const char *CC_FLAG[] = {
    "-std=c23",
    "-O3",
    "-Wall",
    "-Wextra",
    "-Isrc",
    "-Itest",
};
#define CC_FLAG_COUNT (sizeof(CC_FLAG) / sizeof(CC_FLAG[0]))

static const char *D_FLAG[] = { "-g3", "-g", "-pg"};
#define D_FLAG_COUNT (sizeof(D_FLAG) / sizeof(D_FLAG[0]))

Nob_Cmd gcc_flag = {0};

#define SRC_DIR "src"
#define TEST_DIR "test/frame"
#define ELF_PATH "build/main"

static void notice(const char *color, const char *msg) {
    nob_log(NOB_INFO, "%s============== %s ==============%s", color, msg, ANSI_RESET);
}

void global_init(bool is_debug){
    for (uint32_t i = 0; i < CC_FLAG_COUNT; i ++) cmd_append(&gcc_flag, CC_FLAG[i]);
    if (is_debug){
        for (uint32_t i = 0; i < D_FLAG_COUNT; i ++) cmd_append(&gcc_flag, D_FLAG[i]);
    }
}

Nob_Cmd c_file_list = {0};

/* 收集所有c文件 */
bool walk_callback_for_c(Nob_Walk_Entry entry){
    if (entry.type != NOB_FILE_REGULAR) return true;
    String_View sv = sv_from_cstr(entry.path);
    if (sv_ends_with_cstr(sv, ".c")){
        cmd_append(&c_file_list, nob_temp_sprintf("%s", entry.path));
    }
    return true;
}

void show_cmd(Nob_Cmd cmd){
    for (int i = 0; i < cmd.count; i ++){
        printf("%s\n", cmd.items[i]);
    }
    printf("\n");
}

bool elf_build(){
    return nob_walk_dir(SRC_DIR, walk_callback_for_c);
}

bool elf_build_test_frame(){
    return nob_walk_dir(TEST_DIR, walk_callback_for_c);
}

bool elf_compile(const char * main_program){
    Nob_Cmd cmd = {0};
    cmd_append(&cmd, "gcc");
    cmd_extend(&cmd, &c_file_list);
    cmd_extend(&cmd, &gcc_flag);
    cmd_append(&cmd, main_program);
    cmd_append(&cmd, "-o", ELF_PATH);

    if (!nob_cmd_run(&cmd)){
        notice(ANSI_RED, "Compile Failed");
        return false;
    }
    notice(ANSI_GREEN, "Compile Success");
    return true;
}

bool elf_run(int argc, char ** argv){
    // run test
    Nob_Cmd cmd = {0};
    cmd_append(&cmd, nob_temp_sprintf("./%s", ELF_PATH));
    for (int i = 0; i < argc; i ++){
        cmd_append(&cmd, argv[i]);
    }

    if (!nob_cmd_run(&cmd)){
        notice(ANSI_RED, "Running Failed");
        return false;
    }
    
    notice(ANSI_GREEN, "Running Success");
    return true;
}

const char *usage =
"Usage:\n"
"   ./build/nob <command> [options] [arguments]\n"
"\n"
"Commands:\n"
"   run    <main.c> [args...]   Compile all .c files under " SRC_DIR " together with\n"
"                               <main.c>, then run the resulting binary with [args].\n"
"   test   [args...]            Compile and run the test framework located under\n"
"                               " TEST_DIR " with [args].\n"
"   debug  <main.c> [args...]   Same as 'run', but compile with debug flags\n"
"                               (-g3 -g) for use with gdb/lldb.\n"
"\n"
"Arguments:\n"
"   <main.c>    Path to the entry .c file (e.g. src/hashtable.c).\n"
"   [args...]   Arguments forwarded to the compiled program / test runner.\n"
"\n"
"Examples:\n"
"   ./build/nob run main.c\n"
"   ./build/nob run main.c --verbose --count=10\n"
"   ./build/nob test\n"
"   ./build/nob test --filter=MyTest -v\n"
"   ./build/nob debug main.c\n"
"\n"
"Notes:\n"
"   - All .c files found recursively under " SRC_DIR " are compiled.\n"
"   - Test sources are collected recursively from " TEST_DIR ".\n"
"   - Build output: " ELF_PATH "\n";

/* ================ main ================ */
int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    nob_shift_args(&argc, &argv);

    // get debug flag
    if (strcmp(argv[0], "-d") == 0){
        global_init(true);
        nob_shift_args(&argc, &argv);
    }else{
        global_init(false);
    }

    const char * target = "build";
    if (argc >= 0){
        target = argv[0];
        nob_shift_args(&argc, &argv);
    }

    if (argc == 0){
        goto failed;
    }

    if (strcmp(target, "run") == 0) {
        elf_build();
        elf_build_test_frame(); 
        elf_compile(argv[0]);
        elf_run(argc, argv);
    } else if (strcmp(target, "build") == 0){
        elf_build();
        elf_build_test_frame(); 
        elf_compile(argv[0]);
    } else {
        goto failed;
    }

    return 0;
failed:
    notice(ANSI_RED, "unknown command");
    printf("%s", usage);
    return 1;
}
