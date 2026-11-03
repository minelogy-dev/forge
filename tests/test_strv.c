// Source lib/src/strv.c
// Include lib/include
#include <build.h>
#include <forge_strv.h>

#include <string.h>

int main(void) {
  strv_t v = {0};
  TEST(strv_init(&v) == 0);
  TEST(strv_append(&v, "first") == 0);
  TEST(strv_set(&v, v.count, "outside") == -1);
  TEST(v.strs[v.count] == NULL);
  TEST(strv_set(&v, 0, "updated") == 0);
  TEST(strcmp(strv_get(&v, 0), "updated") == 0);

  strv_destroy(&v);
  TEST(v.strs == NULL && v.count == 0 && v.capacity == 0);
  strv_destroy(&v);

  strv_t dest = {0};
  TEST(strv_init(&dest) == 0);
  TEST(strv_append(&dest, "old") == 0);
  TEST(strv_copy(&dest, &v) == 0);
  TEST(dest.strs == NULL && dest.count == 0 && dest.capacity == 0);
  strv_destroy(&dest);
  TEST(strv_init(&dest) == 0);
  TEST(strv_append(&dest, "new") == 0);
  strv_destroy(&dest);
  PASS();
  return 0;
}
