/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#ifndef FORGE_OPT
#define FORGE_OPT

/**
 * @brief Optimization level.
 *
 * Actual command-line flags used by each compiler:
 *
 * | Level         | GCC / CLANG | MSVC |
 * |---------------|-------------|------|
 * | OPT_NONE      | -O0         | /Od  |
 * | OPT_DEBUG     | -Og         | /Zi  |
 * | OPT_SIZE      | -Os         | /O1  |
 * | OPT_SPEED     | -O2         | /O2  |
 * | OPT_AGGRESSIVE| -O3         | /Ox  |
 */
typedef enum {
  OPT_NONE,
  OPT_DEBUG,
  OPT_SIZE,
  OPT_SPEED,
  OPT_AGGRESSIVE
} opt_level_t;

#endif