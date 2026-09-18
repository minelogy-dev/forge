/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file linker.c
 * @brief Linker routing: dispatches on
 *        context.target->toolchain.linker.
 *
 * Same layout convention as compiler.c: the top level only routes;
 * implementations live in lib/src/linker/{gcc,clang}.c. Linking
 * currently happens through the compiler (legacy semantics); each
 * backend is a thin wrapper over a compiler backend. Real ld/lld
 * wrappers are left for later work.
 */
#include "forge_linker.h"
#include "forge_compiler.h"
#include "forge_def.h"

strv_t *linker(const forge_context_t context) {
  if (!context.target)
    return NULL;
  switch (context.target->toolchain.linker) {
  case LD:
    return gcc_linker(context);
  case LLD:
  case CLANG:
    return clang_linker(context);
  default:
    /* unrecognized link chain: fall back to the compiler route (the
       link round dispatches by source_type) */
    return compiler(context);
  }
}