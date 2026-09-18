// Source lib/src/type.c
// Source lib/src/string.c
// Source lib/src/strv.c
// Source lib/src/args.c
// Source lib/src/archiver.c
// Source lib/src/archiver/ar.c
// Source lib/src/archiver/llvm-ar.c
// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Source lib/src/os/windows.c
// Include lib/include
// Include lib/src
#include <build.h>
#include <forge_archiver.h>

#include <string.h>

/* Archive command generation: default rcs; deterministic/verbose flags ->
   modifiers D/v (verifies that the archiver_args fields are actually read
   by the ar/llvm-ar backends) */
static void rt(const forge_context_t ctx, const char *tool, const char *flags) {
  strv_t *seq = tool[0] == 'a' ? ar_archiver(ctx) : llvm_ar_archiver(ctx);
  TEST(seq != NULL);
  TEST(seq[0].strs != NULL);
  TEST(strcmp(seq[0].strs[0], tool) == 0);
  TEST(strcmp(seq[0].strs[1], flags) == 0);
  for (int i = 0; seq[i].strs; i++)
    strv_destroy(&seq[i]);
  free(seq);
}

int main(void) {
  target_t *t = new_target("arch");
  TEST(t != NULL);
  t->toolchain.archiver = AR;

  forge_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.target = t;
  ctx.output_file = "out.a";
  char *objs[] = {"a.o", "b.o", NULL};
  ctx.objs = objs;
  ctx.files_count = 2;

  /* Defaults: both flags 0 -> still rcs */
  rt(ctx, "ar", "rcs");
  rt(ctx, "llvm-ar", "rcs");

  /* deterministic only */
  t->toolchain.archiver_args->deterministic = 1;
  rt(ctx, "ar", "rcDs");
  rt(ctx, "llvm-ar", "rcDs");

  /* deterministic + verbose */
  t->toolchain.archiver_args->verbose = 1;
  rt(ctx, "ar", "rcDvs");
  rt(ctx, "llvm-ar", "rcDvs");

  /* verbose only */
  t->toolchain.archiver_args->deterministic = 0;
  rt(ctx, "ar", "rcvs");
  rt(ctx, "llvm-ar", "rcvs");

  free_target_contents(t);
  PASS();
  return 0;
}