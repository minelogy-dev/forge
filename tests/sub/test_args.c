// Source lib/src/type.c
// Source lib/src/strv.c
// Source lib/src/args.c
// Include lib/include
// Option -O2 -flto=8
#include <build.h>

#include <stdlib.h>
#include <string.h>

int main(void) {
  TEST(atoi("42") == 42);
  TEST(atoi("-7") == -7);

  char *dup = strdup("sub");
  TEST(dup != NULL);
  TEST(strcmp(dup, "sub") == 0);
  free(dup);

  target_t *t = new_target(NULL);
  TEST(t != NULL);
  TEST(t->name == NULL);
  free(t);

  PASS();
  return 0;
}