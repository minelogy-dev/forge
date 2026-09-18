// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Source lib/src/args.c
// Include lib/include
#include <build.h>

/* POSIX-only test (symlink removal semantics): guarded out on
   Windows, where it prints PASSED and skips. Note: this comment must
   stay outside the leading // entry block (parse_test_c only parses
   the run of lines starting with // at the top of the file and stops
   at the first non-// line), otherwise it would be misparsed as DSL
   entries. */

#if UNIX

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char *tmp_join(const char *dir, const char *name) {
  size_t d = strlen(dir);
  size_t n = strlen(name);
  char *p = malloc(d + 1 + n + 1);
  if (!p)
    return NULL;
  memcpy(p, dir, d);
  p[d] = '/';
  memcpy(p + d + 1, name, n + 1);
  return p;
}

static void make_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (f) {
    if (content)
      fputs(content, f);
    fclose(f);
  }
}

int main(void) {
#if UNIX
  char base[64];
  snprintf(base, sizeof(base), "/tmp/forge_os_test_%d", (int)getpid());

  TEST(os_mkdir_r(base) == 0);

  /* A3: symlinks pointing at a file/directory; os_remove deletes only
   the link itself */
  char *f = tmp_join(base, "file.txt");
  char *d = tmp_join(base, "dir");
  char *lf = tmp_join(base, "link_file");
  char *ld = tmp_join(base, "link_dir");
  make_file(f, "x");
  TEST(os_mkdir(d) == 0);
  TEST(symlink(f, lf) == 0);
  TEST(symlink(d, ld) == 0);
  TEST(os_path_is_file(f) == 1);
  TEST(os_path_is_dir(d) == 1);
  TEST(os_remove(lf) == 0);
  TEST(os_path_exists(lf) == 0);
  TEST(os_path_is_file(f) == 1); /* the target file must survive */
  TEST(os_remove(ld) == 0);
  TEST(os_path_exists(ld) == 0);
  TEST(os_path_is_dir(d) == 1); /* the target directory must survive */

  /* os_path_abs normalizes "." / ".." in absolute paths */
  char *abs = os_path_abs("/a/../b");
  TEST(abs != NULL && strcmp(abs, "/b") == 0);
  free(abs);
  abs = os_path_abs(NULL);
  TEST(abs != NULL && os_path_is_absolute(abs) == 1);
  free(abs);

  /* A4: recursive removal is best-effort - an undeletable child must
   not block the other children */
  if (geteuid() != 0) {
    char *tree = tmp_join(base, "tree");
    char *g1 = tmp_join(tree, "good1");
    char *g2 = tmp_join(tree, "good2");
    char *bad = tmp_join(tree, "bad");
    char *keep = tmp_join(bad, "keep");
    TEST(os_mkdir_r(tree) == 0);
    make_file(g1, "1");
    make_file(g2, "2");
    TEST(os_mkdir(bad) == 0);
    make_file(keep, "k");
    TEST(chmod(bad, 0555) == 0);
    TEST(os_remove_r(tree) == -1);
    TEST(os_path_exists(g1) == 0);
    TEST(os_path_exists(g2) == 0);
    TEST(os_path_is_dir(bad) == 1);
    TEST(os_path_is_file(keep) == 1);
    TEST(chmod(bad, 0755) == 0);
    TEST(os_remove_r(tree) == 0);
    free(tree);
    free(g1);
    free(g2);
    free(bad);
    free(keep);
  }

  TEST(os_remove_r(base) == 0);
  free(f);
  free(d);
  free(lf);
  free(ld);
#else
  /* Windows branch: this test is POSIX-only (symlink removal
     semantics); skip until reparse-point equivalents are covered.
     default_test no longer turns red just because the os_* symbol
     link is missing */
#endif
  PASS();
  return 0;
}

#endif /* UNIX */
