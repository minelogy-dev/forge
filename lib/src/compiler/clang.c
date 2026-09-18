/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file compiler/clang.c
 * @brief clang_compiler() implementation: the current CC-compatible
 *        subset simply reuses the gcc backend.
 *
 * The flag subset this project currently uses (-std/-O/-D/-I/-f
 * series/-MMD/-o etc.) has identical semantics in gcc and clang; a
 * separate backend is deferred until a difference shows up.
 */
#include "forge_compiler.h"

strv_t *clang_compiler(const forge_context_t context) {
  return gcc_compiler(context);
}