// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Include lib/include
#include <build.h>
#include "tests_common.h"

/* Real multi-module links catch driver/plugin incompatibilities that argv
   tests cannot. All files remain under the ignored test/edge_* trees. */
static int tool_available(const char *tool) {
  char *argv[] = {(char *)tool, "--version", NULL};
  int status = -1;
  char *output = os_execute_capture_all_status(tool, argv, "", NULL, &status);
  free(output);
  return status == 0;
}

static int build_case(const char *name, int lld, int lto) {
  char *dir = edge_dir(name);
  TEST(dir != NULL);
  char script[2048];
  snprintf(script, sizeof(script),
      "#include <build.h>\n"
      "function(build) {\n"
      "  parse_args(builtin_args);\n"
      "  int result = 0;\n"
      "  target(\"app\") {\n"
      "    set_toolchain(CLANG);\n"
      "    set(TARGET_LINKER, 1, %s);\n"
      "    set(TARGET_LTO, 1, %s);\n"
      "    const char *path = getenv(\"FORGE_TEST_LLD_PATH\");\n"
      "    if (%d && path && *path) set(TARGET_LINKER_PATH, 1, path);\n"
      "    set_optimization(OPT_SPEED);\n"
      "    add_sources_r(\"src\");\n"
      "    result = compile();\n"
      "  }\n"
      "  return result;\n"
      "}\n"
      "set_default(build);\n", lld ? "LLD" : "LD",
      lto ? "ENABLE" : "DISABLE", lld);
  TEST(edge_write(dir, "build.c", script) == 0);
  TEST(edge_write(dir, "build.conf", "compiler=GCC\n") == 0);
  TEST(edge_write(dir, "src/main.c",
                  "int answer(void);\n"
                  "int main(void) { return answer() != 42; }\n") == 0);
  TEST(edge_write(dir, "src/answer.c",
                  "int answer(void) { return 42; }\n") == 0);

  char output[16384];
  TEST(edge_forge(dir, output, sizeof(output)) == 0);
  char *argv[] = {"./make", "-j", "2", NULL};
  int status = edge_run(argv, output, sizeof(output), dir);
  if (status != 0)
    fprintf(stderr, "LTO case %s failed:\n%s\n", name, output);
  TEST(status == 0);
  TEST((strstr(output, "-flto=thin") != NULL) == lto);
  TEST(strstr(output, "-flto=2") == NULL);
  if (lld) {
    TEST(strstr(output, "-fuse-ld=lld") != NULL);
    TEST(strstr(output, "-Wl,--threads=2") != NULL);
    TEST((strstr(output, "-Wl,--thinlto-jobs=2") != NULL) == lto);
    TEST(strstr(output, "-plugin-opt,jobs=") == NULL);
    const char *path = getenv("FORGE_TEST_LLD_PATH");
    if (path && *path) {
      TEST(strstr(output, "--ld-path=") != NULL);
      TEST(strstr(output, path) != NULL);
    }
  } else {
    TEST(strstr(output, "-Wl,-plugin-opt,jobs=2") != NULL);
    TEST(strstr(output, "--thinlto-jobs=") == NULL);
  }

  char *app = os_path_join(dir, "output/app");
  TEST(app != NULL);
  char *run[] = {app, NULL};
  TEST(edge_run(run, output, sizeof(output), dir) == 0);
  free(app);

  if (lld && lto) {
    /* A changed job budget must invalidate the link command's cache. */
    argv[2] = "1";
    TEST(edge_run(argv, output, sizeof(output), dir) == 0);
    TEST(strstr(output, "-Wl,--thinlto-jobs=1") != NULL);
    TEST(strstr(output, "-Wl,--threads=1") != NULL);
    TEST(strstr(output, "-c src/") == NULL);
  }
  free(dir);
  return 0;
}

int main(void) {
  const char *require = getenv("FORGE_TEST_REQUIRE_LTO");
  const char *lld = getenv("FORGE_TEST_LLD_PATH");
  int available = edge_available() && tool_available("clang") &&
                  tool_available(lld && *lld ? lld : "ld.lld");
  if (require && strcmp(require, "1") == 0)
    TEST(available);
  if (!available) {
    fprintf(stderr, "SKIP: Clang/LLD LTO scenarios require a bootstrapped "
                    "Forge, Clang and LLD\n");
  } else {
    TEST(build_case("lto_gnu", 0, 1) == 0);
    TEST(build_case("lto_lld", 1, 1) == 0);
    TEST(build_case("lto_disabled", 1, 0) == 0);
  }
  PASS();
  return 0;
}
