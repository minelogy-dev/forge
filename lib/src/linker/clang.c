/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file linker/clang.c
 * @brief clang_linker() implementation: a placeholder -- reuses the
 *        compiler link branch.
 *
 * Same as linker/gcc.c: linking actually goes through the compiler
 * (the clang backend); a real lld wrapper will replace it later.
 */
#include "forge_linker.h"
#include "forge_compiler.h"

strv_t *clang_linker(const forge_context_t context) {
  return clang_compiler(context);
}