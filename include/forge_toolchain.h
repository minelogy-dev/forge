/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file forge_toolchain.h
 * @brief Toolchain constants and types used by the host tool (the src
 *        directory source tree), self-contained.
 *
 * The host tool's include tree (-I include -I lib/include) and lib's
 * internal include tree (-I lib/include) are disjoint, so this header
 * duplicates the constant definitions from lib/include/forge_def.h and
 * the type declarations from forge_type.h. The duplication on both
 * sides is a known trade-off; keep the values in sync when editing.
 */
#ifndef FORGE_TOOLCHAIN
#define FORGE_TOOLCHAIN

/* ---- Toolchain kind constants (match lib/include/forge_def.h) ---- */

#define UNKNOWN -1
#define GCC     0
#define CLANG   1
#define MSVC    2
#define AR      3
#define LLVM_AR  4
#define LD      5
#define LLD     6
#define LLVM_NM  7
#define NM      8

/* ---- Type/arg-struct forward decls (mirror lib/include/forge_type.h) ---- */

typedef int archiver_type_t;
typedef struct archiver_args_t archiver_args_t;
typedef int assembler_type_t;
typedef struct assembler_args_t assembler_args_t;
typedef int compiler_type_t;
typedef struct compiler_args_t compiler_args_t;
typedef int linker_type_t;
typedef struct linker_args_t linker_args_t;
typedef int symbol_lister_type_t;
typedef struct symbol_lister_args_t symbol_lister_args_t;

#endif /* FORGE_TOOLCHAIN */