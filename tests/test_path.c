// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Source lib/src/os/windows.c
// Source lib/src/args.c
// Include lib/include
#include <build.h>

#include <stdlib.h>
#include <string.h>

static int eq(const char *got, const char *want) {
  if (got == NULL || want == NULL)
    return got == want;
  return strcmp(got, want) == 0;
}

int main(void) {
  char *p;

  p = os_path_join("a", "b");
  TEST(eq(p, "a/b"));
  free(p);
  p = os_path_join("a/", "b");
  TEST(eq(p, "a/b"));
  free(p);
  p = os_path_join("a", "");
  TEST(eq(p, "a"));
  free(p);
  p = os_path_join(NULL, "b");
  TEST(eq(p, "b"));
  free(p);
  p = os_path_join("a", NULL);
  TEST(eq(p, "a"));
  free(p);

  p = os_path_dirname("a/b/c.txt");
  TEST(eq(p, "a/b"));
  free(p);
  p = os_path_dirname("a/b/");
  TEST(eq(p, "a"));
  free(p);
  p = os_path_dirname("/a");
  TEST(eq(p, "/"));
  free(p);
  p = os_path_dirname("a");
  TEST(eq(p, "."));
  free(p);
  p = os_path_dirname("/");
  TEST(eq(p, "/"));
  free(p);
  p = os_path_dirname("C:\\foo.c");
  TEST(eq(p, "C:\\"));
  free(p);
  p = os_path_dirname("C:\\");
  TEST(eq(p, "C:\\"));
  free(p);
  p = os_path_dirname("C:\\foo\\bar.c");
  TEST(eq(p, "C:\\foo"));
  free(p);

  p = os_path_basename("a/b/c.txt");
  TEST(eq(p, "c.txt"));
  free(p);
  p = os_path_basename("a/b/");
  TEST(eq(p, "b"));
  free(p);
  p = os_path_basename("/");
  TEST(eq(p, "/"));
  free(p);
  p = os_path_basename("a");
  TEST(eq(p, "a"));
  free(p);
  p = os_path_basename("C:\\");
  TEST(eq(p, "C:\\"));
  free(p);
  p = os_path_basename("C:\\foo\\bar.c");
  TEST(eq(p, "bar.c"));
  free(p);

  TEST(os_path_is_absolute("/a") == 1);
  TEST(os_path_is_absolute("a") == 0);
  TEST(os_path_is_absolute("") == 0);
  TEST(os_path_is_absolute(NULL) == 0);

  p = os_path_normalize("a/b/../c");
  TEST(eq(p, "a/c"));
  free(p);
  p = os_path_normalize("a/../../b");
  TEST(eq(p, "../b"));
  free(p);
  p = os_path_normalize("/a/../b");
  TEST(eq(p, "/b"));
  free(p);
  p = os_path_normalize("/..");
  TEST(eq(p, "/"));
  free(p);
  p = os_path_normalize("C:\\foo\\..\\bar");
  TEST(eq(p, "C:/bar"));
  free(p);
  p = os_path_normalize("C:\\");
  TEST(eq(p, "C:/"));
  free(p);
  p = os_path_normalize("\\\\server\\share\\x\\..\\y");
  TEST(eq(p, "//server/share/y"));
  free(p);
  p = os_path_normalize("a//b///c");
  TEST(eq(p, "a/b/c"));
  free(p);
  p = os_path_normalize("a/./b");
  TEST(eq(p, "a/b"));
  free(p);
  p = os_path_normalize(".");
  TEST(eq(p, ""));
  free(p);
  p = os_path_normalize("a/b/");
  TEST(eq(p, "a/b"));
  free(p);
  p = os_path_normalize("../../x");
  TEST(eq(p, "../../x"));
  free(p);
  p = os_path_normalize("C:\\foo\\bar.c");
  TEST(eq(p, "C:/foo/bar.c"));
  free(p);

  /* Containment check: pure string prefix (normalized via os_path_abs),
   including equality and root prefix */
  TEST(os_path_is_under("/a/b/c", "/a/b") == 1);
  TEST(os_path_is_under("/a/bc", "/a/b") == 0); /* prefix segment boundary */
  TEST(os_path_is_under("/a/b", "/a/b") == 1);  /* a == b */
  TEST(os_path_is_under("/b", "/a") == 0);
  TEST(os_path_is_under("/a/b/../c", "/a") == 1); /* decided after normalization */
  TEST(os_path_is_under("/x", "/") == 1);         /* root prefix */
  TEST(os_path_is_under(NULL, "/a") == 0);

  /* Relative path: when under b, return the relative part preserving
   separators; otherwise NULL */
  p = os_path_rel_under("/a/b/c", "/a/b");
  TEST(eq(p, "c"));
  free(p);
  p = os_path_rel_under("/a/b/c/d", "/a");
  TEST(eq(p, "b/c/d"));
  free(p);
  p = os_path_rel_under("/a/x/../b", "/a");
  TEST(eq(p, "b"));
  free(p);
  p = os_path_rel_under("/a/b", "/a/b"); /* empty relative -> NULL */
  TEST(eq(p, NULL));
  free(p);
  p = os_path_rel_under("/b", "/a");
  TEST(eq(p, NULL));
  free(p);
  p = os_path_rel_under(NULL, "/a");
  TEST(eq(p, NULL));
  free(p);

  /* Flattening: separator -> '_', '_' -> "%5F", '%' -> "%25" */
#if defined(FORGE_OS_WINDOWS)
  p = os_path_flatten("C:\\usr\\x_y.c");
  TEST(eq(p, "C:_usr_x%5Fy.c"));
  free(p);
  p = os_path_flatten("C:\\a%b_c\\d.c");
  TEST(eq(p, "C:_a%25b%5Fc_d.c"));
  free(p);
#else
  p = os_path_flatten("/usr/x_y.c");
  TEST(eq(p, "_usr_x%5Fy.c"));
  free(p);
  p = os_path_flatten("/a%b_c/d.c");
  TEST(eq(p, "_a%25b%5Fc_d.c"));
  free(p);
#endif

  /* File destination: mirrored inside the project, flattened outside
   it; extension replaced or kept */
  char *cwd = os_path_abs(NULL);
  TEST(cwd != NULL);
  char *abs = os_path_join(cwd, "src/build/main.c");
  p = os_path_file_rel(abs, ".o");
  TEST(eq(p, "src/build/main.o"));
  free(p);
  p = os_path_file_rel(abs, NULL); /* NULL ext keeps the original name */
  TEST(eq(p, "src/build/main.c"));
  free(p);
  p = os_path_file_rel(abs, "");
  TEST(eq(p, "src/build/main.c"));
  free(p);
  p = os_path_file_rel(cwd, ".o"); /* file is root: empty relative destination fails */
  TEST(eq(p, NULL));
  free(p);
  free(abs);
  free(cwd);

#if defined(FORGE_OS_WINDOWS)
  p = os_path_file_rel("C:\\forge-out\\x_y.c", ".o");
  TEST(eq(p, "C:_forge-out_x%5Fy.o"));
  free(p);
  p = os_path_file_rel("C:\\forge-out\\a.b\\c", ".o"); /* '.' does not cross separators */
  TEST(eq(p, "C:_forge-out_a.b_c.o"));
  free(p);
#else
  p = os_path_file_rel("/forge-out/x_y.c", ".o");
  TEST(eq(p, "_forge-out_x%5Fy.o"));
  free(p);
  p = os_path_file_rel("/forge-out/a.b/c", ".o"); /* '.' does not cross separators */
  TEST(eq(p, "_forge-out_a.b_c.o"));
  free(p);
#endif

  PASS();
  return 0;
}
