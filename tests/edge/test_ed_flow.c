// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Include lib/include
#include <build.h>
#include <forge_os.h>
#include "tests_common.h"

#include <string.h>

/* S2a high-coupling end-to-end flow: the complete lifecycle from
   creating a project to just before release, run as one linear
   sequence. Complements the low-coupling scenarios: this one
   verifies how the stages chain together - any failed step breaks
   the chain (coarse localization; test_ed_config/runtime pin down
   details). */

static void skip_all(void) {
  fprintf(stderr, "SKIP: build/output/forge unavailable (run ./bootstrap.sh first)\n");
  PASS();
  exit(0);
}

static int out_has(const char *out, const char *sub) {
  return out && strstr(out, sub) != NULL;
}

/* Project: src/main.c gets its message from h.h; the build script
   uses target("app") */
static const char *FLOW_BUILD_C =
    "#include <build.h>\n"
    "function(build) {\n"
    "  target(\"app\") {\n"
    "    set_toolchain(GCC);\n"
    "    add_sources_r(\"src\");\n"
    "    add_include_path(\"include\");\n"
    "    set_optimization(OPT_SPEED);\n"
    "    if (compile() != 0) return -1;\n"
    "  }\n"
    "  return 0;\n"
    "}\n"
    "set_default(build);\n";

static const char *FLOW_H =
    "#ifndef FLOW_H\n#define FLOW_H\n"
    "static const char *flow_msg(void) { return \"flow-ok\"; }\n"
    "#endif\n";

static const char *FLOW_MAIN_V1 =
    "#include <flow.h>\n#include <stdio.h>\n"
    "int main(void) { puts(flow_msg()); return 0; }\n";

static int run_app(const char *dir, char *out, size_t outsz) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/output/app", dir);
  char *args[] = {path, NULL};
  int st;
  char *cap =
      os_execute_capture_all_status(args[0], args, "", NULL, &st);
  if (cap) {
    strncpy(out, cap, outsz - 1);
    out[outsz - 1] = '\0';
    free(cap);
  } else if (outsz) {
    out[0] = '\0';
  }
  return st;
}

int main(void) {
  if (!edge_available())
    skip_all();

  char out[16384];
  char *dir = edge_dir("flow_main");
  TEST(dir != NULL);
  TEST(edge_write(dir, "build.c", FLOW_BUILD_C) == 0);
  TEST(edge_write(dir, "build.conf", "compiler=GCC\n") == 0);
  TEST(edge_write(dir, "include/flow.h", FLOW_H) == 0);
  TEST(edge_write(dir, "src/main.c", FLOW_MAIN_V1) == 0);

  /* 1. forge . -> ./make full build -> the binary prints flow-ok */
  TEST(edge_forge(dir, out, sizeof(out)) == 0);
  TEST(edge_make(dir, "make", out, sizeof(out)) == 0);
  TEST(run_app(dir, out, sizeof(out)) == 0);
  TEST(strstr(out, "flow-ok") != NULL);

  /* 2. Second ./make run is idempotent: no compile lines */
  TEST(edge_make(dir, "make", out, sizeof(out)) == 0);
  TEST(!out_has(out, "-c src/main.c"));

  /* 3. Touch the source -> ./make incrementally rebuilds that file and
   the binary is updated */
  TEST(edge_write(dir, "src/main.c",
                  "#include <flow.h>\n#include <stdio.h>\n"
                  "int main(void) { puts(flow_msg()); puts(\"v2\"); return 0; }\n") == 0);
  TEST(edge_make(dir, "make", out, sizeof(out)) == 0);
  TEST(out_has(out, "main.o"));  /* main.c was rebuilt */
  TEST(run_app(dir, out, sizeof(out)) == 0);
  TEST(strstr(out, "v2") != NULL);

  /* 4. Touch the header -> ./make incrementally rebuilds (header
   dependency detection - fixed 2026-09-18) */
  TEST(edge_write(dir, "include/flow.h",
                  "#ifndef FLOW_H\n#define FLOW_H\n"
                  "static const char *flow_msg(void) { return \"flow-h2\"; }\n"
                  "#endif\n") == 0);
  TEST(edge_make(dir, "make", out, sizeof(out)) == 0);
  TEST(out_has(out, "main.o"));
  TEST(run_app(dir, out, sizeof(out)) == 0);
  TEST(strstr(out, "flow-h2") != NULL);

  /* 5. Corrupt .meta -> conservative rebuild from the opt cache,
   success */
  {
    char *mp = os_path_join(dir, "opt/app/src/main.o.meta");
    TEST(mp != NULL);
    FILE *f = fopen(mp, "w");
    TEST(f != NULL);
    fputs("garbage\n", f);
    fclose(f);
    free(mp);
  }
  TEST(edge_make(dir, "make", out, sizeof(out)) == 0);
  TEST(out_has(out, "-c src/main.c"));

  /* 6. ./make --help -> 0; unknown function -> non-zero with an
   error */
  TEST(edge_make(dir, "--help", out, sizeof(out)) == 0);
  TEST(out_has(out, "Usage"));
  int st = edge_make(dir, "nope", out, sizeof(out));
  TEST(st != 0);
  TEST(out_has(out, "not found") || out_has(out, "undefined"));

  free(dir);
  PASS();
  return 0;
}