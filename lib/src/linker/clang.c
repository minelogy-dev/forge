/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file linker/clang.c
 * @brief Clang link command generation through the compiler driver.
 *
 * The shared link branch uses Clang's backend policy to select an
 * explicitly requested LLD and to translate LTO/parallelism options.
 */
#include "forge_linker.h"
#include "forge_compiler.h"

strv_t *clang_linker(const forge_context_t context) {
  return clang_compiler(context);
}
