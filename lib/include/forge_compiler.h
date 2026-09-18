/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#ifndef FORGE_COMPILER
#define FORGE_COMPILER

#include "forge_type.h"

struct compiler_args_t {
  toolchain_t *toolchain;
  strv_t defines;
  opt_level_t optimization; /**< optimization level; default is OPT_NONE */
  int debug_enabled;
  int pic_enabled;
  int pie_enabled;
  int lto_enabled;
  int visibility;    /**< visibility: HIDDEN / DEFAULT (default visible) */
  char *standard;
  strv_t options;    /**< extra args appended at the end of the command */
                      /**< (//Option entry contract) */
};

strv_t* gcc_compiler(const forge_context_t context);
strv_t* clang_compiler(const forge_context_t context);
/** Stub: not tested on Windows, disabled before release, always returns      */
/** NULL (see compiler/msvc.c).                                               */
strv_t* msvc_compiler(const forge_context_t context);

/**
 * @brief Generate the compile command.
 *
 * Routes to the appropriate generator based on the target information in
 * context.
 *
 * @param[in] context compile context (list and types of source files).
 * @return malloc-allocated compile command string (multiple commands are
 *         joined with '\n'); the caller must free it; NULL on failure.
 */
strv_t* compiler(const forge_context_t context);

#endif
