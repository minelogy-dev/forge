/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file archiver/ar.c
 * @brief ar_archiver() implementation:
 *        `ar <modifiers> <output> <obj...>`.
 *
 * Modifiers: r insert + c create silently + s symbol table (the
 * minimal set needed for bootstrapping); D = deterministic
 * timestamps, v = verbose, driven by the archiver_args->deterministic
 * / verbose switches; with both off the command is still `ar rcs`
 * (behaving like the old implementation).
 */
#include "forge_archiver.h"
#include "internal.h"

#include <stdio.h>

strv_t *ar_archiver(const forge_context_t context) {
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

  failed = strv_append(&args, "ar") != 0;
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
  return argv_to_cmdline(&args);
}