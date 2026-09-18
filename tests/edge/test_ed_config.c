// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Include lib/include
#include <build.h>
#include <forge_os.h>
#include "tests_common.h"

#include <string.h>

/* S2a low-coupling config branch: every scenario gets its own
   temporary directory, its own forge invocation, and its own
   assertions - a failure pins down exactly one scenario. Prerequisite:
   build/output/forge and bootstrap-prefix exist (all cases SKIP
   otherwise). */

static void skip_all(void) {
  fprintf(stderr, "SKIP: build/output/forge unavailable (run ./bootstrap.sh first)\n");
  PASS();
  exit(0);
}

/* Assert that out contains substring */
static void expect_contains(const char *out, const char *sub, const char *what) {
  if (!strstr(out ? out : "", sub)) {
    fprintf(stderr, "FAIL: %s expected to contain <%s>; actual output:\n%s\n",
            what, sub,
            out ? out : "");
    exit(1);
  }
}

/* Overwrite dir/build.conf */
static int set_conf(const char *dir, const char *content) {
  return edge_write(dir, "build.conf", content);
}

/* Mandatory template init: create a valid project (build.c + conf)
   and return dir (caller asserts TEST(dir) and frees it). Earlier,
   several scenarios omitted build.c, so the tests never reached the
   intended bugs (proven painful to debug); routing everything through
   this entry makes that impossible. */
static char *edge_init_conf(const char *name, const char *conf) {
  char *d = edge_dir(name);
  if (!d)
    return NULL;
  if (edge_write(d, "build.c",
                 "#include <build.h>\nfunction(build){return 0;}\n"
                 "set_default(build);\n") != 0)
    goto fail;
  if (conf && edge_write(d, "build.conf", conf) != 0)
    goto fail;
  return d;
fail:
  free(d);
  return NULL;
}

int main(void) {
  if (!edge_available())
    skip_all();

  char out[8192];
  char *dir = NULL;
  int st;

  /* 1. build.conf missing -> explicit error, not silence */
  dir = edge_dir("cfg_no_conf");
  TEST(dir && edge_write(dir, "build.c",
    "#include <build.h>\nfunction(build){return 0;}\nset_default(build);\n") == 0);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st != 0);
  expect_contains(out, "Can't open", "no_conf");
  free(dir);

  /* 2. Empty build.conf -> default GCC fallback, success */
  dir = edge_dir("cfg_empty_conf");
  TEST(dir && edge_write(dir, "build.c",
    "#include <build.h>\nfunction(build){return 0;}\nset_default(build);\n") == 0);
  TEST(edge_write(dir, "build.conf", "") == 0);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st == 0);
  TEST(os_path_is_file(os_path_join(dir, "make")));
  free(dir);

  /* 3. Unknown keys / lines without '=' / comments -> tolerated,
   success */
  dir = edge_dir("cfg_filler");
  TEST(dir && edge_write(dir, "build.c",
    "#include <build.h>\nfunction(build){return 0;}\nset_default(build);\n") == 0);
  TEST(edge_write(dir, "build.conf",
                  "garbage=1\nno-equals line\n# whole comment\n") == 0);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st == 0);
  free(dir);

  /* 4. compiler=unknown -> explicit error, no silent fallback to the
   default */
  dir = edge_init_conf("cfg_unkcomp", "compiler=unknown\n");
  TEST(dir != NULL);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st != 0);
  expect_contains(out, "unknown compiler", "unknown_compiler");
  free(dir);

  /* 5. compiler=CLANG cross-build -> success (when clang exists) */
  dir = edge_dir("cfg_clang");
  TEST(dir &&
       edge_write(dir, "build.c",
         "#include <build.h>\nfunction(build){return 0;}\nset_default(build);\n") == 0 &&
       set_conf(dir, "compiler=CLANG\ncompiler_path=/usr/bin/clang\n") == 0);
  if (os_path_is_file("/usr/bin/clang")) {
    st = edge_forge(dir, out, sizeof(out));
    TEST(st == 0);
  } else {
    fprintf(stderr, "SKIP: /usr/bin/clang not found\n");
  }
  free(dir);

  /* 6. Nonexistent compiler_path -> non-zero exit (exec failure is
   diagnosable) */
  dir = edge_init_conf("cfg_badcc", "compiler=GCC\ncompiler_path=/nonexistent/cc\n");
  TEST(dir != NULL);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st != 0);
  free(dir);

  /* 7/8. output_name empty string / containing a path separator ->
   rejected */
  dir = edge_init_conf("cfg_on_empty", "output_name=\n");
  TEST(dir != NULL);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st != 0);
  expect_contains(out, "invalid output_name", "outname_empty");
  free(dir);

  dir = edge_init_conf("cfg_on_slash", "output_name=a/b\n");
  TEST(dir != NULL);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st != 0);
  expect_contains(out, "invalid output_name", "outname_slash");
  free(dir);

  /* 9. Valid custom output_name -> the matching executable is built
   and usable */
  dir = edge_init_conf("cfg_on_custom", "output_name=mybuild\n");
  TEST(dir != NULL);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st == 0);
  char *mb = os_path_join(dir, "mybuild");
  TEST(mb && os_path_is_file(mb));
  free(mb);
  free(dir);

  /* 10. Project path containing spaces -> the whole chain succeeds */
  os_remove_r("test/edge_space dir");
  TEST(os_mkdir_r("test/edge_space dir") == 0);
  char *sp = os_path_abs("test/edge_space dir");
  TEST(sp && edge_write(sp, "build.c",
    "#include <build.h>\nfunction(build){return 0;}\nset_default(build);\n") == 0 &&
    edge_write(sp, "build.conf", "compiler=GCC\n") == 0);
  char *sargs[] = {"build/output/forge", sp, NULL};
  int sst;
  char *sout =
      os_execute_capture_all_status(sargs[0], sargs, "", NULL, &sst);
  free(sout);
  TEST(sst == 0);
  free(sp);

  /* 11. Missing version key -> fallback, compile succeeds */
  dir = edge_init_conf("cfg_vmissing", "compiler=GCC\n");
  TEST(dir != NULL);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st == 0);
  free(dir);

  /* 12. version=v0.5 (injected without quotes) -> printed via STR
   stringification inside the script */
  dir = edge_dir("cfg_vset");
  TEST(dir && set_conf(dir, "compiler=GCC\nversion=v0.5\n") == 0);
  TEST(edge_write(dir, "build.c",
    "#include <build.h>\n#include <stdio.h>\n"
    "#define STR_(t) #t\n#define STR(t) STR_(t)\n"
    "function(version) { puts(STR(CONF_version)); return 0; }\n"
    "function(build){return 0;}\nset_default(build);\n") == 0);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st == 0);
  st = edge_make(dir, "version", out, sizeof(out));
  TEST(st == 0);
  expect_contains(out, "v0.5", "version_set");
  free(dir);

  PASS();
  return 0;
}