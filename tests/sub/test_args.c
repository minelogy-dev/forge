// Source lib/src/type.c
// Source lib/src/strv.c
// Source lib/src/args.c
// Include lib/include
// Option -O2 -flto
#include <build.h>

#include <stdlib.h>
#include <string.h>

int main(void) {
  /* Short options do not require a corresponding long option. */
  opt_t opts[OPTS_COUNT] = {
      ['x'] = {.long_name = NULL},
      ['y'] = {.long_name = NULL},
      ['o'] = {.long_name = NULL, .takes_arg = 1},
  };
  char *args[] = {"test", "-x", "-xy", "-oattached", "-o", "split", NULL};
  TEST(forge_parse_args(6, args, opts, OPTS_COUNT) == 6);
  TEST(opts['x'].flag_store == 2);
  TEST(opts['y'].flag_store == 1);
  TEST(opts['o'].argc == 2);
  TEST(strcmp(opts['o'].argv[0], "attached") == 0);
  TEST(strcmp(opts['o'].argv[1], "split") == 0);
  free(opts['o'].argv);

  TEST(atoi("42") == 42);
  TEST(atoi("-7") == -7);

  char *dup = strdup("sub");
  TEST(dup != NULL);
  TEST(strcmp(dup, "sub") == 0);
  free(dup);

  target_t *t = new_target(NULL);
  TEST(t != NULL);
  TEST(t->name == NULL);
  free_target_contents(t);

  PASS();
  return 0;
}
