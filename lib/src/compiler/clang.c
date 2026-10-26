/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file compiler/clang.c
 * @brief Clang policy over the shared GCC-style command builder.
 *
 * Common flags share the CC builder; LTO and linker selection stay here.
 * The public LTO switch selects ThinLTO for Clang, independently of the
 * GCC backend's implementation of the same build intent.
 */
#include "forge_compiler.h"
#include "forge_def.h"
#include "forge_linker.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int forge_clang_options(strv_t *args, const forge_context_t context) {
  const toolchain_t *tc = &context.target->toolchain;
  if (context.source_type == FORGE_SOURCE)
    return tc->compiler_args->lto_enabled == ENABLE
               ? strv_append(args, "-flto=thin") : 0;

  if (tc->linker == LLD && strv_append(args, "-fuse-ld=lld") != 0)
    return -1;
  if (tc->linker_path && *tc->linker_path) {
    const char prefix[] = "--ld-path=";
    size_t size = sizeof(prefix) + strlen(tc->linker_path);
    char *option = malloc(size);
    if (!option)
      return -1;
    snprintf(option, size, "%s%s", prefix, tc->linker_path);
    int rc = strv_append(args, option);
    free(option);
    if (rc != 0)
      return -1;
  }

#if defined(__linux__)
  /* LLD's worker budget applies even when LTO is disabled. The selector,
     rather than a guessed executable name, identifies ELF LLD here. */
  if (tc->linker == LLD) {
    char option[64];
    snprintf(option, sizeof(option), "-Wl,--threads=%d", os_jobs);
    if (strv_append(args, option) != 0)
      return -1;
  }
#endif

  if (tc->linker_args->lto_enabled != ENABLE)
    return 0;
  if (strv_append(args, "-flto=thin") != 0)
    return -1;

#if defined(__linux__)
  char option[64];
  /* LLVM's GNU linker plugin and ELF LLD use different spellings for
     the same ThinLTO backend budget. Honor -j 1 as well as -j N. */
  snprintf(option, sizeof(option), tc->linker == LLD
               ? "-Wl,--thinlto-jobs=%d" : "-Wl,-plugin-opt,jobs=%d", os_jobs);
  return strv_append(args, option);
#else
  /* Other platform backends are not enabled in this Linux-only release. */
  return 0;
#endif
}

strv_t *clang_compiler(const forge_context_t context) {
  return gcc_compiler(context);
}
