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
#include <forge_compiler.h>

#include <string.h>

/* Helper: assert the position of "arg plus its following value" in the
   argv sequence */
static int find_arg(strv_t *v, const char *arg, int at) {
  int n = 0;
  for (; v->strs[n]; n++) {
    if (strcmp(v->strs[n], arg) == 0 && at < 0) {
      /* Require the next value to exist at at (ambiguity left to the
         caller) */
      if (v->strs[n + 1])
        return n;
      return -1;
    }
    if (at >= 0 && strcmp(v->strs[n], arg) == 0 && n == at)
      return n;
  }
  return -1;
}

static int count_args(strv_t *v) {
  int n = 0;
  for (; v->strs[n]; n++)
    ;
  return n;
}

static void free_seq(strv_t *seq) {
  for (int i = 0; seq[i].strs; i++)
    strv_destroy(&seq[i]);
  free(seq);
}

/* Compile-leg context: single file + output obj */
static void mk_ctx(forge_context_t *ctx, target_t *t, char *obj) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->target = t;
  ctx->output_file = obj;
  static source_t flat[1];
  flat[0].file = "main.c";
  strv_init(&flat[0].cflags);
  ctx->files = flat;
  ctx->files_count = 1;
  ctx->source_type = FORGE_SOURCE; /* compile-leg convention */
  ctx->target_type = FORGE_OBJ;
}

int main(void) {
  target_t *t = new_target("app");
  TEST(t != NULL);

  forge_context_t ctx;
  mk_ctx(&ctx, t, "out/app/main.o");

  /* 1. Default GCC compile leg: cc -> -O0 (default OPT_NONE) -> -c ->
     source -> deterministic seed -> -o -> -MMD -MF */
  strv_t *seq = gcc_compiler(ctx);
  TEST(seq != NULL && seq[0].strs != NULL);
  TEST(strcmp(seq[0].strs[0], "gcc") == 0);
  TEST(find_arg(&seq[0], "-c", -1) >= 0);
  TEST(find_arg(&seq[0], "-O0", -1) >= 0);
  int oi = find_arg(&seq[0], "-o", -1);
  TEST(oi >= 0 && seq[0].strs[oi + 1] &&
       strcmp(seq[0].strs[oi + 1], "out/app/main.o") == 0);
  TEST(find_arg(&seq[0], "-frandom-seed=out/app/main.o", -1) >= 0);
  TEST(find_arg(&seq[0], "-MMD", -1) >= 0);
  /* The source must appear after -c and before -o */
  int ci = find_arg(&seq[0], "-c", -1);
  TEST(ci >= 0 && ci + 1 < count_args(&seq[0]) &&
       strcmp(seq[0].strs[ci + 1], "main.c") == 0);
  free_seq(seq);

  /* 2. Injection chain: define (two args, value carries its own
     quotes) + include + per-file cflags */
  TEST(_add_define(t, "APP", "\"forge\"") == 0);
  TEST(_add_include_path(t, "include/demo") == 0);
  ctx.files[0].cflags.count = 0; /* reset (struct-array reuse) */
  TEST(strv_append(&ctx.files[0].cflags, "-O3") == 0);
  seq = gcc_compiler(ctx);
  TEST(seq != NULL);
  TEST(find_arg(&seq[0], "-DAPP=\"forge\"", -1) >= 0); /* value has quotes */
  TEST(find_arg(&seq[0], "-Iinclude/demo", -1) >= 0);
  TEST(find_arg(&seq[0], "-O3", -1) >= 0); /* per-file cflags take effect */
  /* cflags sit after -I and before visibility/mode flags: verify the
     relative order */
  int io = find_arg(&seq[0], "-Iinclude/demo", -1);
  int o3 = find_arg(&seq[0], "-O3", -1);
  TEST(io >= 0 && o3 > io);
  free_seq(seq);
  strv_destroy(&ctx.files[0].cflags);

  /* 3. Dispatcher: compiler() routes on toolchain.compiler */
  seq = compiler(ctx);
  TEST(seq != NULL); /* default route: GCC */
  free_seq(seq);
  t->toolchain.compiler = CLANG;
  seq = compiler(ctx);
  TEST(seq != NULL && strcmp(seq[0].strs[0], "clang") == 0);
  free_seq(seq);

  /* 4. MSVC backend: stub (disabled, never exercised on this platform).
     The dispatcher routing to MSVC must yield NULL - the command
     generation failure path reports an explicit error instead of
     emitting a deceptively usable command */
  t->toolchain.compiler = MSVC;
  memset(&ctx.files[0].cflags, 0, sizeof(strv_t));
  seq = compiler(ctx);
  TEST(seq == NULL);

  free_target_contents(t);
  PASS();
  return 0;
}