/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#ifndef FORGE_ASSEMBLER
#define FORGE_ASSEMBLER

#include "forge_type.h"

struct assembler_args_t {
    toolchain_t *toolchain;

    int debug_enabled;               /**< generate debug info (DWARF); */
                                     /**< GCC: -g, LLVM: -g, MSVC: N/A */
    int syntax_intel;                /**< use Intel syntax; */
                                     /**< GCC: -msyntax=intel, LLVM: */
                                     /**< -x86-asm-syntax=intel; default AT&T */
    int no_execstack;                /**< mark the stack non-executable; */
                                     /**< GCC: -Wa,--noexecstack, LLVM: */
                                     /**< -no-exec-stack, MSVC: N/A */
    int fatal_warnings;              /**< treat warnings as errors; GCC: */
                                     /**< -Wa,--fatal-warnings, LLVM: */
                                     /**< -Werror, MSVC: N/A */
    int statistics;                  /**< output statistics; */
                                     /**< GCC: -Wa,--stats, LLVM: */
                                     /**< -stats */

    strv_t options;                  /**< extra user-defined assembler */
                                     /**< options (passed through) */
};

#endif