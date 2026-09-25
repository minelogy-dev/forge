/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file compiler/gcc.c
 * @brief gcc_compiler() implementation: GCC-style compile/link
 *        command generation.
 *
 * A fresh implementation (the old compiler.c is not reused).
 * Commands are always assembled via the argument table ->
 * argv_to_cmdline (the os_argv2line contract; only " and \ are
 * special).
 *
 * Two branches, dispatched on context.source_type:
 * - Compile branch (FORGE_SOURCE, single-file ctx): cc -> -std ->
 *   optimization -> -D -> -I (target+libs) -> per-file cflags ->
 *   visibility/lto/pic flags -> -c|-S -> source file -> -o obj ->
 *   -MMD -MF obj.d -> trailing options;
 * - Link branch (FORGE_OBJ, EXE/SHARED): cc -> -flto(=N when
 *   os_jobs>1) -> -shared -> obj list -> -L (incl. libs) -> -l -> -o ->
 *   export symbol (os_export_flag) -> trailing linker options.
 * FORGE_STATIC_LIB belongs to the archiver; NULL is returned here.
 *
 * Argument order matches the old semantics (content unchanged):
 * the -MMD dependency flags only reach this backend on non-MSVC;
 * MSVC uses compiler/msvc.c.
 */
#include "forge_compiler.h"
#include "forge_linker.h"
#include "forge_os.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Prefix-concatenation arguments (e.g. -I/, -D): appended to args;
   -1 on failure */
static int append_prefix(strv_t *args, const char *prefix, const char *value) {
  size_t pl = strlen(prefix), vl = strlen(value);
  char *tok = malloc(pl + vl + 1);
  if (!tok)
    return -1;
  memcpy(tok, prefix, pl);
  memcpy(tok + pl, value, vl + 1);
  int rc = strv_append(args, tok);
  free(tok);
  return rc;
}

/* optimization level -> GCC flag */
static const char *opt_flag(opt_level_t o) {
  static const char *opts[] = {"-O0", "-Og", "-Os", "-O2", "-O3"};
  if (o < OPT_NONE || o > OPT_AGGRESSIVE)
    return NULL;
  return opts[o];
}

/* compile branch: single-file ctx, producing .o/.obj or .s (asm_out) */
static strv_t *gcc_compile(const forge_context_t ctx) {
  const target_t *t = ctx.target;
  const compiler_args_t *ca = t->toolchain.compiler_args;
  if (!ctx.files || ctx.files_count != 1 || !ctx.output_file)
    return NULL;
  const source_t *f = &ctx.files[0]; /* ctx.files points at the
                                        first struct-array element */
  if (!f->file || !*f->file)
    return NULL;

  int failed = 0;
  strv_t args;
  strv_init(&args);

  failed = strv_append(&args, forge_default_cc(t)) != 0;

  if (!failed && ca->standard && *ca->standard) {
    char tok[128];
    snprintf(tok, sizeof(tok), "-std=%s", ca->standard);
    failed = strv_append(&args, tok) != 0;
  }
  const char *opt = opt_flag(ca->optimization);
  if (!failed && opt)
    failed = strv_append(&args, opt) != 0;

  for (int i = 0; !failed && i < ca->defines.count; i++)
    failed = append_prefix(&args, "-D", ca->defines.strs[i]) != 0;
  for (int i = 0; !failed && i < t->include_paths.count; i++)
    failed = append_prefix(&args, "-I", t->include_paths.strs[i]) != 0;
  for (int i = 0; !failed && i < t->libs_count; i++) {
    const library_t *lib = t->libs[i];
    for (int j = 0; !failed && j < lib->include_paths.count; j++)
      failed = append_prefix(&args, "-I", lib->include_paths.strs[j]) != 0;
  }
  for (int i = 0; !failed && i < f->cflags.count; i++)
    failed = strv_append(&args, f->cflags.strs[i]) != 0;

  if (!failed && ca->visibility == HIDDEN)
    failed = strv_append(&args, "-fvisibility=hidden") != 0;
  if (!failed && ca->lto_enabled == ENABLE)
    failed = strv_append(&args, "-flto") != 0;
  if (!failed && (ca->pic_enabled != 0 ||
                  ctx.target_type == FORGE_SHARED_LIB))
    failed = strv_append(&args, "-fPIC") != 0;

  if (!failed) {
    const char *mode = ctx.target_type == FORGE_ASM ? "-S" : "-c";
    failed = strv_append(&args, mode) != 0;
  }
  if (!failed) {
    char *src = shorten_path(ctx.root, f->file);
    failed = !src || strv_append(&args, src) != 0;
    free(src);
  }
  if (!failed) {
    char *obj = shorten_path(ctx.root, ctx.output_file);
    /* Determinism: without -frandom-seed the LTO partition names of
       -flto are random on every run, making the object bytes
       unstable. A fixed seed = the target path, so the same path
       reproduces the same bytes */
    char seed[4096];
    if (!obj)
      failed = 1;
    else if (snprintf(seed, sizeof(seed), "-frandom-seed=%s", obj) >=
             (int)sizeof(seed))
      failed = 1;
    else
      failed = strv_append(&args, seed) != 0;
    if (!failed) {
      failed = strv_append(&args, "-o") != 0;
      failed = failed || strv_append(&args, obj) != 0;
    }
    free(obj);
  }
  if (!failed) {
    char *obj = shorten_path(ctx.root, ctx.output_file);
    size_t dl = obj ? strlen(obj) : 0;
    char *dpath = malloc(dl + 3);
    if (!obj || !dpath) {
      failed = 1;
    } else {
      memcpy(dpath, obj, dl);
      memcpy(dpath + dl, ".d", 3);
      failed = strv_append(&args, "-MMD") != 0;
      failed = failed || strv_append(&args, "-MF") != 0;
      failed = failed || strv_append(&args, dpath) != 0;
    }
    free(obj);
    free(dpath);
  }
  for (int i = 0; !failed && i < t->options.count; i++)
    failed = strv_append_args(&args, t->options.strs[i]) != 0;
  for (int i = 0; !failed && i < ca->options.count; i++)
    failed = strv_append_args(&args, ca->options.strs[i]) != 0;

  if (failed) {
    strv_destroy(&args);
    return NULL;
  }
  return argv_to_cmdline(&args);
}

/* link branch: ctx.files is a struct array (length files_count);
   the parallel ctx.objs carries the obj paths (replacing the
   deprecated source_t.obj_file) */
static strv_t *gcc_link(const forge_context_t ctx) {
  const target_t *t = ctx.target;
  const linker_args_t *la = t->toolchain.linker_args;
  if (!ctx.output_file || !*ctx.output_file)
    return NULL;
  if (ctx.target_type == FORGE_STATIC_LIB)
    return NULL; /* belongs to the archiver */

  int failed = 0;
  strv_t args;
  strv_init(&args);

  failed = strv_append(&args, forge_default_cc(t)) != 0;

  if (!failed && la->lto_enabled == ENABLE) {
    if (os_jobs > 1) {
      char flto[32];
      snprintf(flto, sizeof(flto), "-flto=%d", os_jobs);
      failed = strv_append(&args, flto) != 0;
    } else {
      failed = strv_append(&args, "-flto") != 0;
    }
  }
  if (!failed && ctx.target_type == FORGE_SHARED_LIB)
    failed = strv_append(&args, "-shared") != 0;

  for (int i = 0; !failed && i < ctx.files_count; i++) {
    const char *obj = ctx.objs ? ctx.objs[i] : NULL;
    if (obj) {
      char *short_obj = shorten_path(ctx.root, obj);
      failed = !short_obj || strv_append(&args, short_obj) != 0;
      free(short_obj);
    }
  }
  for (int i = 0; !failed && i < t->lib_paths.count; i++)
    failed = append_prefix(&args, "-L", t->lib_paths.strs[i]) != 0;
  for (int i = 0; !failed && i < t->libs_count; i++) {
    const library_t *lib = t->libs[i];
    for (int j = 0; !failed && j < lib->lib_paths.count; j++)
      failed = append_prefix(&args, "-L", lib->lib_paths.strs[j]) != 0;
  }
  for (int i = 0; !failed && i < t->link_libs.count; i++)
    failed = append_prefix(&args, "-l", t->link_libs.strs[i]) != 0;

  if (!failed) {
    char *out = shorten_path(ctx.root, ctx.output_file);
    failed = !out;
    if (!failed) {
      failed = strv_append(&args, "-o") != 0;
      failed = failed || strv_append(&args, out) != 0;
    }
    free(out);
  }
  if (!failed)
    failed = strv_append(&args, os_export_flag()) != 0;
  for (int i = 0; !failed && i < t->options.count; i++)
    failed = strv_append_args(&args, t->options.strs[i]) != 0;
  for (int i = 0; !failed && i < la->options.count; i++)
    failed = strv_append_args(&args, la->options.strs[i]) != 0;

  if (failed) {
    strv_destroy(&args);
    return NULL;
  }
  return argv_to_cmdline(&args);
}

strv_t *gcc_compiler(const forge_context_t context) {
  if (!context.target || !context.files || context.files_count <= 0)
    return NULL;
  if (context.source_type == FORGE_SOURCE)
    return gcc_compile(context);
  return gcc_link(context);
}