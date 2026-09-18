// Source lib/src/type.c
// Source lib/src/string.c
// Source lib/src/strv.c
// Source lib/src/args.c
// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Source lib/src/os/windows.c
// Include lib/include
// Include lib/src
#include <build.h>
#include <forge_os.h>
#include <forge_strv.h>
#include "internal.h"

#include <stdlib.h>
#include <string.h>

/* NULL-robustness matrix (frozen). Contract: public APIs must not
   crash on NULL inputs and must return deterministic sentinels; the
   strv family uses a uniform failure code of -1 (bad argument) / 1
   (NULL string) and returns NULL for value-returning calls. Behavior
   is observed before it is asserted (settled by live measurement on
   2026-09-13). */

int main(void) {
  strv_t v;
  char *r = NULL;

  /* ---- strv family ---- */
  TEST(strv_new() != NULL);
  strv_free(NULL); /* must be a no-op */
  TEST(strv_init(NULL) == -1);
  strv_destroy(NULL);        /* must be a no-op */
  strv_destroy(&v);          /* an uninitialized stack strv must also be safe */
  TEST(strv_append(NULL, "x") == 1);
  TEST(strv_init(&v) == 0);
  TEST(strv_append(&v, NULL) == 1);
  TEST(strv_append(&v, "a") == 0);
  TEST(strv_set(&v, 0, NULL) == -1);
  TEST(strv_set(NULL, 0, "x") == -1);
  TEST(strv_set(&v, -1, "x") == -1);
  TEST(strv_get(NULL, 0) == NULL);
  TEST(strv_get(&v, -1) == NULL);
  TEST(strv_find(&v, NULL) == -1);
  TEST(strv_find(NULL, "a") == -1);
  TEST(strv_replace_first(&v, NULL, "r") == -1);
  TEST(strv_replace_first(&v, "a", NULL) == -1);
  TEST(strv_replace_first(NULL, "a", "r") == -1);
  TEST(strv_replace_all(&v, NULL, "r") == -1);
  TEST(strv_replace_all(&v, "a", NULL) == -1);
  TEST(strv_replace_all(NULL, "a", "r") == -1);
  TEST(strv_copy(&v, NULL) == -1);
  TEST(strv_copy(NULL, &v) == -1);
  TEST(strv_append_non_copy(NULL, "x") == 1);
  TEST(strv_append_non_copy(&v, NULL) == 1);

  /* ---- os_path family (NULL side: skipped / sentinel) ---- */
  r = os_path_join(NULL, "b");
  TEST(r != NULL && strcmp(r, "b") == 0);
  free(r);
  r = os_path_join("a", NULL);
  TEST(r != NULL && strcmp(r, "a") == 0);
  free(r);
  TEST(strcmp(os_path_basename(NULL), "") == 0); /* empty string, not NULL */
  TEST(os_path_is_absolute(NULL) == 0);
  TEST(os_path_is_under(NULL, "b") == 0);
  TEST(os_path_is_under("a", NULL) == 1); /* frozen observed value: NULL acts as an empty root */
  TEST(os_path_flatten(NULL) == NULL);
  TEST(os_path_file_rel(NULL, "o") == NULL);

  /* ---- line / string family ---- */
  TEST(os_line2argv(NULL, NULL) == NULL);
  TEST(os_argv2line(NULL) == NULL);
  TEST(forge_strdup(NULL) == NULL);
  TEST(strv_append_args(NULL, "x") == -1);
  TEST(strv_append_args(&v, NULL) == -1);

  /* ---- type family ---- */
  TEST(source_new(NULL) == NULL);

  strv_destroy(&v);
  PASS();
  return 0;
}