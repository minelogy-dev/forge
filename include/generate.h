/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file generate.h
 * @brief Thin wrappers for compiling and linking with GCC, CLANG, MSVC.
 *
 * The actual compile/link commands are run as subprocesses via
 * os_execute_raw(), and the compiler's stdout/stderr pass straight
 * through to the current terminal. Object file extension: GCC/CLANG
 * produce .o, MSVC produces .obj.
 */
#ifndef FORGE_GENERATE
#define FORGE_GENERATE

#include "forge_toolchain.h"

/**
 * @brief Compile a set of source files, producing the corresponding
 *        object files under output_dir.
 *
 * Runs one compile command per source file target:
 * - GCC / CLANG: <compiler_path> [argv] -c <target> -o <output_dir>/<name>.o
 * - MSVC:        <compiler_path> [argv] -c <target> Fo<output_dir>/<name>.obj
 *
 * where <name> is the source file name (without directory/extension).
 *
 * @param[in] compiler_path Compiler executable path ("/usr/bin/gcc").
 * @param[in] compiler      Compiler kind: GCC, CLANG or MSVC.
 * @param[in] targets       NULL-terminated source file paths array.
 * @param[in] output_dir    Directory for object files; current if NULL.
 * @param[in] argv          NULL-terminated extra compile flags
 *                          (e.g. "-O2"); may be NULL.
 * @return 0 if all compiles succeed; otherwise the failing compile's
 *         subprocess exit code; -1 on invalid arguments or if the
 *         process could not be started.
 */
int gen_compile(char *compiler_path, int compiler, char **targets,
                char *output_dir, char **argv);

/**
 * @brief Link a set of object files into an executable.
 *
 * - GCC / CLANG: <compiler_path> [argv] <targets...> -o <output_file>
 * - MSVC:        <compiler_path> [argv] <targets...> Fe<output_file>
 *
 * @param[in] compiler_path Path to the compiler (also the linker).
 * @param[in] compiler      Compiler kind: GCC, CLANG or MSVC.
 * @param[in] targets       NULL-terminated object paths (.o / .obj).
 * @param[in] output_file   Path of the executable to produce.
 * @param[in] argv          NULL-terminated extra link flags;
 *                          may be NULL.
 * @return 0 on success; the linker exit code on failure; -1 on
 *         invalid arguments or if the process could not be started.
 */
int gen_link(char *compiler_path, int compiler, char **targets,
             char *output_file, char **argv);

#endif