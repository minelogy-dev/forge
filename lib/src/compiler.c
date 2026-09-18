/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file compiler.c
 * @brief Compiler routing: dispatches on
 *        context.target->toolchain.compiler.
 *
 * Layout convention: a component's top-level file only contains the
 * route function; all implementations live in the
 * lib/src/compiler/{gcc,clang,msvc}.c backend directory. The old
 * command-generation logic is deprecated and not reused.
 */
#include "forge_compiler.h"
#include "forge_def.h"

strv_t *compiler(const forge_context_t context) {
  if (!context.target)
    return NULL;
  switch (context.target->toolchain.compiler) {
  case GCC:
    return gcc_compiler(context);
  case CLANG:
    return clang_compiler(context);
  case MSVC:
    return msvc_compiler(context);
  default:
    return NULL;
  }
}