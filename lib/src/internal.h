/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file internal.h
 * @brief Internal utilities for the libforge runtime (not installed).
 *
 * String buffer, command-line build/split and execution helpers used
 * by the .c files inside lib/src. Not public API; not shipped via
 * lib/include.
 */
#ifndef FORGE_LIB_INTERNAL
#define FORGE_LIB_INTERNAL

#include "forge_type.h"

#include <stddef.h>

/**
 * @brief Growable string buffer.
 */
typedef struct {
  char *buf;  /**< contents (always '\0'-terminated, NULL when empty) */
  size_t len; /**< current length (excluding the trailing '\0') */
  size_t cap; /**< allocated capacity */
} strbuf;

/** @brief Initialize to an empty buffer. */
void sb_init(strbuf *sb);
/** @brief Free the buffer and reset it. */
void sb_free(strbuf *sb);
/** @brief Append a string; returns -1 on failure. */
int sb_cat(strbuf *sb, const char *s);
/** @brief Append fixed-length content; returns -1 on failure. */
int sb_catn(strbuf *sb, const char *s, size_t n);
/** @brief Append a single character; returns -1 on failure. */
int sb_char(strbuf *sb, char c);
/**
 * @brief Wrap the argument table into a command-sequence strv_t*
 * array under the os_argv2line contract (element 0 = args itself,
 * terminated by an empty strv_t); see lib/src/string.c.
 *
 * @param[in] args the argument table (elements are strdup'ed; on
 *            failure the function frees all of them).
 * @return malloc'ed strv_t array (the caller strv_destroys each
 *         element, then frees the array); NULL on failure.
 */
strv_t *argv_to_cmdline(strv_t *args);

/**
 * @brief Shorten a command argument path: when path is inside root,
 * returns the path relative to root (malloc'ed); otherwise returns a
 * verbatim copy; when root is NULL, also verbatim. Used by the
 * command-generation backends when building argv (caller frees).
 */
char *shorten_path(const char *root, const char *path);

/**
 * @brief Split text into multiple arguments by the command-line
 * quoting rules and append them to args one by one.
 *
 * Trailing argument entries such as //Option keep the old
 * "append verbatim, then split for execution" semantics (e.g.
 * "-O2 -flto=8" is two arguments); whitespace inside an entry
 * splits via os_line2argv (only " and \\ are special). See
 * lib/src/string.c.
 *
 * @return 0 on success, -1 on failure.
 */
int strv_append_args(strv_t *args, const char *text);

/**
 * @brief Return the default compiler executable name for a target.
 *
 * When compiler_path is set it is returned verbatim; otherwise the
 * compiler type and language pick "gcc" / "clang" / "cl" ("g++" /
 * "clang++" for C++).
 *
 * @param[in] t the target configuration.
 * @return a static string the caller must not modify or free.
 */
const char *forge_default_cc(const target_t *t);

/**
 * @brief Return whether the current MSVC compiler supports
 * /sourceDependencies (generating .d dependencies).
 *
 * cl 19.27+ (VS2019 16.7) supports /sourceDependencies. How it
 * works: run `<cc> /Bv` with merged output capture
 * (os_execute_capture_all) to obtain the version banner (on
 * stderr), locate "Version " and parse MAJOR.MINOR against the
 * threshold. The result is cached per compiler_path in-process
 * (re-checked when the path changes); a failed capture/parse
 * always counts as unsupported (returns 0), in which case build.c
 * writes an empty .d after a successful compile.
 *
 * @param[in] t the target configuration (must be MSVC).
 * @return 1 when supported; 0 when unsupported or not MSVC.
 */
int forge_msvc_source_deps_supported(const target_t *t);

#endif
