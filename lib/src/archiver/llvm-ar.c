/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file archiver/llvm-ar.c
 * @brief llvm_ar_archiver() implementation:
 *        `llvm-ar <modifiers> <output> <obj...>`.
 *
 * The modifiers are the same as in ar.c (r/c/s/D/v; llvm-ar
 * supports the same combination syntax), driven by the
 * archiver_args->deterministic / verbose switches; `rcs` by
 * default.
 */
#include "forge_archiver.h"
#include "internal.h"

#include <stdio.h>

strv_t *llvm_ar_archiver(const forge_context_t context) {
  if (!context.output_file || !*context.output_file)
    return NULL;
  int deterministic = 0, verbose = 0;
  if (context.target && context.target->toolchain.archiver_args) {
    deterministic = context.target->toolchain.archiver_args->deterministic;
    verbose = context.target->toolchain.archiver_args->verbose;
  }
  char flags[8];
  snprintf(flags, sizeof(flags), "rc%s%s%s", deterministic ? "D" : "",
           verbose ? "v" : "", "s");
  int failed = 0;
  strv_t args;
  strv_init(&args);

  failed = strv_append(&args, "llvm-ar") != 0;
  if (!failed)
    failed = strv_append(&args, flags) != 0;
  if (!failed) {
    char *out = shorten_path(context.root, context.output_file);
    failed = !out || strv_append(&args, out) != 0;
    free(out);
  }
  for (int i = 0; !failed && i < context.files_count; i++) {
    const char *obj = context.objs ? context.objs[i] : NULL;
    if (obj) {
      char *short_obj = shorten_path(context.root, obj);
      failed = !short_obj || strv_append(&args, short_obj) != 0;
      free(short_obj);
    }
  }
  if (failed) {
    strv_destroy(&args);
    return NULL;
  }

  /* Same rationale as in ar_archiver (see ar.c): ar rcs keeps stale
     members, so the archive is rebuilt from scratch every time. */
  strv_t rm;
  strv_init(&rm);
  failed = strv_append(&rm, "rm") != 0;
  if (!failed) {
    char *out = shorten_path(context.root, context.output_file);
    failed = !out || strv_append(&rm, "-f") != 0 ||
             strv_append(&rm, out) != 0;
    free(out);
  }
  if (failed) {
    strv_destroy(&args);
    strv_destroy(&rm);
    return NULL;
  }
  strv_t *seq = calloc(3, sizeof(strv_t));
  if (!seq) {
    strv_destroy(&args);
    strv_destroy(&rm);
    return NULL;
  }
  seq[0] = rm;
  seq[1] = args;
  return seq;
}