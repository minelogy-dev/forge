// Source lib/src/type.c
// Source lib/src/string.c
// Source lib/src/strv.c
// Source lib/src/args.c
// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Source lib/src/os/windows.c
// Include lib/include
#include <build.h>
#include <forge_def.h>
#include <forge_compiler.h>
#include <forge_linker.h>

#include <string.h>

int main(void) {
  TEST(new_target(NULL) != NULL);

  target_t *t = new_target("abc");
  TEST(t != NULL);
  TEST(t->name != NULL);
  TEST(strcmp(t->name, "abc") == 0);
  TEST(t->target_type == FORGE_EXECUTABLE);
  TEST(t->language == C);

  /* Toolchain family defaults and the five args structures */
  TEST(t->toolchain.compiler == GCC);
  TEST(t->toolchain.archiver == AR);
  TEST(t->toolchain.assembler == GCC);
  TEST(t->toolchain.linker == LD);
  TEST(t->toolchain.symbol_lister == NM);
  TEST(t->toolchain.compiler_args != NULL);
  TEST(t->toolchain.linker_args != NULL);
  TEST(t->toolchain.archiver_args != NULL);
  TEST(t->toolchain.assembler_args != NULL);
  TEST(t->toolchain.symbol_lister_args != NULL);
  TEST(t->toolchain.compiler_args->optimization == OPT_NONE);
  TEST(t->toolchain.compiler_args->visibility == DEFAULT);
  TEST(t->toolchain.compiler_args->lto_enabled == ENABLE);
  TEST(t->toolchain.linker_args->lto_enabled == ENABLE);
  TEST(t->toolchain.compiler_path == NULL);

  /* All strvs are initialized (append usable) and the source list is
   empty */
  TEST(t->include_paths.strs != NULL);
  TEST(t->lib_paths.strs != NULL);
  TEST(t->link_libs.strs != NULL);
  TEST(t->options.strs != NULL);
  TEST(t->exports.strs != NULL);
  TEST(t->sources == NULL && t->sources_tail == NULL);

  target_t *copy = new_target("abc");
  TEST(copy != NULL);
  TEST(copy->name != NULL && strcmp(copy->name, t->name) == 0);
  TEST(copy->name != t->name);

  free_target_contents(copy);
  free_target_contents(t);
  PASS();
  return 0;
}