// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Include lib/include
#include <build.h>
#include <forge_os.h>
#include "tests_common.h"

#include <string.h>

/* S2a low-coupling runtime branch: the behavior contract of the
   generated ./make, one scenario each. */

static void skip_all(void) {
  fprintf(stderr, "SKIP: build/output/forge unavailable (run ./bootstrap.sh first)\n");
  PASS();
  exit(0);
}

static int out_has(const char *out, const char *sub) {
  return out && strstr(out, sub) != NULL;
}

/* No-compile template: a single function, no targets (verifies
   default dispatch and argument behavior) */
static const char *TPL =
    "#include <build.h>\nfunction(build){return 0;}\nset_default(build);\n";

/* Real-compile template: target("app") + src/main.c (verifies cache
   semantics) */
static const char *REAL_TPL =
    "#include <build.h>\n"
    "function(build) {\n"
    "  target(\"app\") {\n"
    "    set_toolchain(GCC);\n"
    "    add_sources_r(\"src\");\n"
    "    set_optimization(OPT_SPEED);\n"
    "    if (compile() != 0) return -1;\n"
    "  }\n"
    "  return 0;\n"
    "}\n"
    "set_default(build);\n";

static const char *MAIN_C = "#include <stdio.h>\nint main(void){return 0;}\n";

static int init_real(const char *name) {
  char *dir = edge_dir(name);
  if (!dir)
    return -1;
  int r = edge_write(dir, "build.c", REAL_TPL);
  if (r == 0)
    r = edge_write(dir, "build.conf", "compiler=GCC\n");
  if (r == 0)
    r = edge_write(dir, "src/main.c", MAIN_C);
  free(dir);
  return r;
}

int main(void) {
  if (!edge_available())
    skip_all();

  char out[8192];
  char *dir = NULL;
  int st;

  /* 1/2. Empty build.c and comment-only build.c -> generation
     succeeds, but running with no default function must fail (an
     explicit, diagnosable error - never a vague pass) */
  dir = edge_dir("rt_empty");
  TEST(dir && edge_write(dir, "build.c", "") == 0 &&
       edge_write(dir, "build.conf", "compiler=GCC\n") == 0);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st == 0);
  st = edge_make(dir, "make", out, sizeof(out));
  TEST(st != 0);
  TEST(out_has(out, "undefined symbol") || out_has(out, "not found"));
  free(dir);

  dir = edge_dir("rt_comment");
  TEST(dir && edge_write(dir, "build.c", "// just a comment\n") == 0 &&
       edge_write(dir, "build.conf", "compiler=GCC\n") == 0);
  st = edge_forge(dir, out, sizeof(out));
  TEST(st == 0);
  st = edge_make(dir, "make", out, sizeof(out));
  TEST(st != 0);
  TEST(out_has(out, "undefined symbol") || out_has(out, "not found"));
  free(dir);

  /* 3. Idempotence: build a real project twice; the second ./make run
   must show no compile lines */
  TEST(init_real("rt_idem") == 0);
  dir = os_path_abs("test/edge_rt_idem");
  TEST(edge_forge(dir, out, sizeof(out)) == 0);
  TEST(edge_make(dir, "make", out, sizeof(out)) == 0);
  TEST(edge_make(dir, "make", out, sizeof(out)) == 0);
  TEST(!out_has(out, "-c src/main.c"));
  free(dir);

  /* 4. Corrupt .meta -> conservative rebuild, success (rebuild = a
   compile line in the output) */
  TEST(init_real("rt_metabad") == 0);
  dir = os_path_abs("test/edge_rt_metabad");
  TEST(edge_forge(dir, out, sizeof(out)) == 0);
  TEST(edge_make(dir, "make", out, sizeof(out)) == 0);
  {
    char *mp = os_path_join(dir, "opt/app/src/main.o.meta");
    TEST(mp != NULL);
    FILE *f = fopen(mp, "w");
    TEST(f != NULL);
    fputs("garbage-not-a-command\n", f);
    fclose(f);
    free(mp);
  }
  st = edge_make(dir, "make", out, sizeof(out));
  TEST(st == 0);
  TEST(out_has(out, "-c src/main.c"));
  free(dir);

  /* 5. Corrupt .d -> conservative rebuild, success */
  TEST(init_real("rt_dbad") == 0);
  dir = os_path_abs("test/edge_rt_dbad");
  TEST(edge_forge(dir, out, sizeof(out)) == 0);
  TEST(edge_make(dir, "make", out, sizeof(out)) == 0);
  {
    char *dp = os_path_join(dir, "opt/app/src/main.o.d");
    TEST(dp != NULL);
    FILE *f = fopen(dp, "w");
    TEST(f != NULL);
    fputs("garbage\ndeps\n", f);
    fclose(f);
    free(dp);
  }
  st = edge_make(dir, "make", out, sizeof(out));
  TEST(st == 0);
  TEST(out_has(out, "-c src/main.c"));
  free(dir);

  /* 6. ./make --help -> a help request alone is success (exit 0) */
  dir = edge_dir("rt_help");
  TEST(dir && edge_write(dir, "build.c", TPL) == 0 &&
       edge_write(dir, "build.conf", "compiler=GCC\n") == 0);
  TEST(edge_forge(dir, out, sizeof(out)) == 0);
  st = edge_make(dir, "--help", out, sizeof(out));
  TEST(st == 0);
  TEST(out_has(out, "Usage"));
  free(dir);

  /* 7. Unknown function -> explicit error, non-zero exit */
  dir = edge_dir("rt_nofunc");
  TEST(dir && edge_write(dir, "build.c", TPL) == 0 &&
       edge_write(dir, "build.conf", "compiler=GCC\n") == 0);
  TEST(edge_forge(dir, out, sizeof(out)) == 0);
  st = edge_make(dir, "nope", out, sizeof(out));
  TEST(st != 0);
  TEST(out_has(out, "not found") || out_has(out, "undefined"));
  free(dir);

  PASS();
  return 0;
}