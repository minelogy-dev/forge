// Source lib/src/type.c
// Source lib/src/string.c
// Source lib/src/strv.c
// Source lib/src/args.c
// Source lib/src/build.c
// Source lib/src/archiver.c
// Source lib/src/archiver/ar.c
// Source lib/src/archiver/llvm-ar.c
// Source lib/src/assembler.c
// Source lib/src/symbol_lister.c
// Source lib/src/compiler.c
// Source lib/src/compiler/gcc.c
// Source lib/src/compiler/clang.c
// Source lib/src/compiler/msvc.c
// Source lib/src/linker.c
// Source lib/src/linker/gcc.c
// Source lib/src/linker/clang.c
// Source lib/src/os/line.c
// Source lib/src/os/path.c
// Source lib/src/os/unix.c
// Source lib/src/os/windows.c
// Include lib/include
// Include lib/src
#include <build.h>
#include <forge_linker.h>

#include <string.h>

static int find_arg(strv_t *v, const char *arg) {
  for (int n = 0; v->strs[n]; n++)
    if (strcmp(v->strs[n], arg) == 0)
      return n;
  return -1;
}

static void free_seq(strv_t *seq) {
  for (int i = 0; seq[i].strs; i++)
    strv_destroy(&seq[i]);
  free(seq);
}

/* Link-leg context: obj list + output artifact */
static void mk_ctx(forge_context_t *ctx, target_t *t, file_type_t outtype) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->target = t;
  ctx->output_file = "out/libapp.so";
  static char *objs[] = {"a.o", "b.o", NULL}; /* must be static: a stack array would go out of scope */
  ctx->objs = objs;
  /* The link leg requires ctx.files (a struct array parallel to objs,
     length files_count) */
  static source_t flat[2];
  memset(flat, 0, sizeof(flat));
  strv_init(&flat[0].cflags);
  strv_init(&flat[1].cflags);
  ctx->files = flat;
  ctx->files_count = 2;
  ctx->source_type = FORGE_OBJ; /* link-leg convention: source_type is not SOURCE */
  ctx->target_type = outtype;
}

int main(void) {
  target_t *t = new_target("libapp");
  TEST(t != NULL);
  t->toolchain.linker = LD;

  forge_context_t ctx;
  mk_ctx(&ctx, t, FORGE_SHARED_LIB);

  /* 1. Default GCC link leg: gcc -> -shared -> objs -> -o output;
     the export flag (os_export_flag) must be included */
  strv_t *seq = gcc_linker(ctx);
  TEST(seq != NULL && seq[0].strs != NULL);
  TEST(strcmp(seq[0].strs[0], "gcc") == 0);
  TEST(find_arg(&seq[0], "-shared") >= 0);
  TEST(find_arg(&seq[0], "a.o") >= 0 && find_arg(&seq[0], "b.o") >= 0);
  TEST(find_arg(&seq[0], "-rdynamic") >= 0); /* os_export_flag (Linux) */
  int oi = find_arg(&seq[0], "-o");
  TEST(oi >= 0 && seq[0].strs[oi + 1] &&
       strcmp(seq[0].strs[oi + 1], "out/libapp.so") == 0);
  free_seq(seq);

  /* 2. -L/-l and exported-symbol injection */
  TEST(_add_lib_path(t, "third/party/lib") == 0);
  TEST(_add_link_lib(t, "m") == 0);
  TEST(strv_append(&t->toolchain.linker_args->export_symbol, "function_build") ==
       0);
  seq = gcc_linker(ctx);
  TEST(seq != NULL);
  TEST(find_arg(&seq[0], "-Lthird/party/lib") >= 0);
  TEST(find_arg(&seq[0], "-lm") >= 0);
  free_seq(seq);

  /* 3. Clang link leg (reuses the gcc backend's argument subset; both
     the cc name and the linker type come from the genuine value
     domain in forge_def.h: CLANG=1; the dead LD_LD/LD_CLANG enum is
     gone) */
  t->toolchain.linker = CLANG;
  t->toolchain.compiler = CLANG;
  seq = linker(ctx);
  TEST(seq != NULL && strcmp(seq[0].strs[0], "clang") == 0);
  TEST(find_arg(&seq[0], "-shared") >= 0);
  free_seq(seq);

  /* 4. Dispatcher: linker() routes on toolchain.linker */
  t->toolchain.linker = LD;
  t->toolchain.compiler = GCC;
  seq = linker(ctx);
  TEST(seq != NULL && strcmp(seq[0].strs[0], "gcc") == 0);
  free_seq(seq);

  free_target_contents(t);
  PASS();
  return 0;
}