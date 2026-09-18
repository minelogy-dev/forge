/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file archiver.c
 * @brief Archiver routing: dispatches on
 *        context.target->toolchain.archiver.
 *
 * Same layout convention as compiler.c: the top level only routes;
 * implementations live in lib/src/archiver/{ar,llvm-ar}.c. The MSVC
 * lib.exe branch is a TODO (to be implemented after testing on
 * Windows).
 */
#include "forge_archiver.h"
#include "forge_def.h"

strv_t *archiver(const forge_context_t context) {
  if (!context.target)
    return NULL;
  switch (context.target->toolchain.archiver) {
  case AR:
    return ar_archiver(context);
  case LLVM_AR:
    return llvm_ar_archiver(context);
  default:
    return NULL; /* TODO: the MSVC lib.exe branch */
  }
}