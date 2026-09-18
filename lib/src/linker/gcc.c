/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file linker/gcc.c
 * @brief gcc_linker() implementation: a placeholder -- reuses the
 *        compiler link branch.
 *
 * Placeholder: linking is currently done with a compiler command
 * (gcc_compiler generates the link command when
 * source_type==FORGE_OBJ); a real ld wrapper will replace it later.
 */
#include "forge_linker.h"
#include "forge_compiler.h"

strv_t *gcc_linker(const forge_context_t context) {
  return gcc_compiler(context);
}