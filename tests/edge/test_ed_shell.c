// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Include lib/include
#include <build.h>
#include <forge_os.h>

#include <string.h>

/* Exercise the empty buffer, separate pipes and buffer growth paths. */
int main(void) {
  TEST(os_shell(NULL) == NULL);

  char *empty[] = {"true", NULL};
  char *out = os_shell(empty);
  TEST(out && strcmp(out, "") == 0);
  free(out);

  char *both[] = {"sh", "-c", "printf out; printf err >&2", NULL};
  out = os_shell(both);
  TEST(out && strcmp(out, "errout") == 0);
  free(out);

  char *large[] = {"seq", "1", "4096", NULL};
  out = os_shell(large);
  TEST(out && strlen(out) > 8192);
  TEST(strncmp(out, "1\n2\n", 4) == 0);
  TEST(strstr(out, "4095\n4096\n") != NULL);
  size_t lines = 0;
  for (const char *p = out; *p; p++)
    if (*p == '\n')
      lines++;
  TEST(lines == 4096);
  free(out);

  PASS();
  return 0;
}
