// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Source lib/src/os/windows.c
// Source lib/src/args.c
// Source lib/src/string.c
// Source lib/src/strv.c
// Include lib/include
#include <build.h>

#include <stdlib.h>
#include <string.h>

/* Round-trip assertion: os_line2argv(os_argv2line(orig)) restores every
   argument */
static void rt(const char *in[]) {
  char **orig = (char **)in;
  char *line = os_argv2line(orig);
  TEST(line != NULL);
  int n = 0;
  char **back = os_line2argv(line, &n);
  TEST(back != NULL);
  int i = 0;
  while (orig[i] && back[i]) {
    TEST(strcmp(orig[i], back[i]) == 0);
    i++;
  }
  TEST(orig[i] == NULL && back[i] == NULL);
  free(line);
  for (int k = 0; k < n; k++)
    free(back[k]);
  free(back);
}

int main(void) {
  /* Plain: no quotes when there are no special characters */
  static const char *a0[] = {"cmd", "-j4", "src", NULL};
  /* Argument with spaces: wrapped as a whole */
  static const char *a1[] = {"gcc", "-o", "my file.o", "a b.c", NULL};
  /* Quoted argument: escaped \", restored via the CRT odd/even run
     rule */
  static const char *a2[] = {"cc", "-DNAME=\"quoted\"", NULL};
  /* Backslash path: kept verbatim when not adjacent to a quote */
  static const char *a3[] = {"gcc", "-I", "C:\\path\\to\\inc", NULL};
  /* Backslash adjacent to a quote: run-aware decoding */
  static const char *a4[] = {"x", "a\\\"b", NULL};
  /* Trailing backslash: after the parity patch, the closing quote is
     parsed as a delimiter */
  static const char *a5[] = {"x", "dir\\", NULL};
  /* Mix of lone backslash, lone quote, space, and empty string */
  static const char *a6[] = {"x", "\\", "\"", " ", "", NULL};
  /* Non-ASCII across common scripts: Latin with diacritics, CJK
     (3-byte UTF-8), Greek, Cyrillic, plus a path with a space.
     None of these bytes are quote/backslash, so they must pass
     through untouched */
  static const char *a7[] = {"café", "naïve", "straße", "中文", "日本語",
                             "한국어", "Ελληνικά", "Русский",
                             "路径 空格.txt", NULL};
  static const char *a8[] = {"a\tb", NULL};
  /* Multiple backslashes adjacent to a quote */
  static const char *a9[] = {"x", "\\\\\"", NULL};
  /* Arguments containing shell metacharacters (not split, not
     expanded) */
  static const char *a10[] = {"sh", "-c", "printf '%s\\n' a b", NULL};

  rt(a0);
  rt(a1);
  rt(a2);
  rt(a3);
  rt(a4);
  rt(a5);
  rt(a6);
  rt(a7);
  rt(a8);
  rt(a9);
  rt(a10);

#if UNIX
  /* os_shell: returns err first, then out, concatenated (not
     interleaved) */
  static const char *s1[] = {"sh", "-c", "echo ERRLINE 1>&2; echo OUTLINE",
                             NULL};
  char *res = os_shell((char **)s1);
  TEST(res != NULL);
  TEST(strncmp(res, "ERRLINE\n", 8) == 0);
  TEST(strstr(res, "OUTLINE\n") != NULL);
  TEST(strcmp(res, "ERRLINE\nOUTLINE\n") == 0);
  free(res);

  /* os_shell: quoted/space arguments behave correctly under sh -c */
  static const char *s2[] = {"printf", "%s\\n", "a b", NULL};
  res = os_shell((char **)s2);
  TEST(res != NULL);
  TEST(strcmp(res, "a b\n") == 0);
  free(res);
#endif /* UNIX */

  PASS();
  return 0;
}