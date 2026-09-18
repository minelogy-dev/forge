/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file string.c
 * @brief String buffer / toolchain command helpers (the internal.h
 *        implementations).
 *
 * Generic utilities from the old compiler.c that have nothing to do
 * with command generation moved into this file: strbuf,
 * forge_default_cc, forge_msvc_source_deps_supported, and
 * argv_to_cmdline (the shared argument-table -> command-line helper
 * used by the command-generation backends, assembled under
 * os_argv2line's "only \" and \\ are special" contract).
 */
#include "forge_os.h"
#include "forge_def.h" /* GCC/CLANG/MSVC constants */
#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------- string buffer ----------------------------- */

void sb_init(strbuf *sb) {
  sb->buf = NULL;
  sb->len = 0;
  sb->cap = 0;
}

void sb_free(strbuf *sb) {
  free(sb->buf);
  sb_init(sb);
}

static int sb_reserve(strbuf *sb, size_t need) {
  if (sb->len + need + 1 <= sb->cap)
    return 0;
  size_t ncap = sb->cap ? sb->cap : 64;
  while (ncap < sb->len + need + 1)
    ncap *= 2;
  char *nb = realloc(sb->buf, ncap);
  if (!nb)
    return -1;
  sb->buf = nb;
  sb->cap = ncap;
  return 0;
}

int sb_catn(strbuf *sb, const char *s, size_t n) {
  if (sb_reserve(sb, n))
    return -1;
  memcpy(sb->buf + sb->len, s, n);
  sb->len += n;
  sb->buf[sb->len] = '\0';
  return 0;
}

int sb_cat(strbuf *sb, const char *s) { return sb_catn(sb, s, strlen(s)); }

int sb_char(strbuf *sb, char c) { return sb_catn(sb, &c, 1); }

/* ---------------------------- compiler paths ---------------------------- */

const char *forge_default_cc(const target_t *t) {
  if (t->toolchain.compiler_path)
    return t->toolchain.compiler_path;
  switch (t->toolchain.compiler) {
  case CLANG:
    return t->language == CPP ? "clang++" : "clang";
  case MSVC:
    return "cl";
  default:
    return t->language == CPP ? "g++" : "gcc";
  }
}

/* --------------------- MSVC dependency detection --------------------- */

/* Parse "Version MAJOR.MINOR" from the cl /Bv output banner to tell
   whether it is >= 19.27 (VS2019 16.7, the first release to support
   /sourceDependencies); returns 0 when unparseable */
static int parse_bv_version(const char *out) {
  if (!out)
    return 0;
  const char *v = strstr(out, "Version ");
  if (!v)
    return 0;
  v += strlen("Version ");
  int major = 0, minor = 0;
  if (sscanf(v, "%d.%d", &major, &minor) != 2)
    return 0;
  return (major > 19) || (major == 19 && minor >= 27);
}

int forge_msvc_source_deps_supported(const target_t *t) {
  if (!t || t->toolchain.compiler != MSVC)
    return 0;

  const char *cc = forge_default_cc(t);
  static char *cached_path = NULL;
  static int cached_support = 0;
  if (cached_path && strcmp(cached_path, cc) == 0)
    return cached_support;

  int support = 0;
  char *argv[] = {(char *)cc, "/Bv", NULL};
  char *out = os_execute_capture_all((char *)cc, argv, "", NULL);
  if (out) {
    support = parse_bv_version(out);
    free(out);
  }

  free(cached_path);
  cached_path = forge_strdup(cc);
  cached_support = support;
  return support;
}

/* ------------------- argument table -> command sequence ------------------- */

/* Wrap args (the argument table, i.e. a set of argv) into a command
   sequence strv_t* array (contract): element 0 = args itself
   (ownership transferred), element 1 = an "empty strv_t"
   (strs=NULL, idx=0, length=0) as the termination sentinel marking
   the end of the sequence. The caller frees: strv_destroy each
   element, then free the array itself. On failure returns NULL and
   frees args. */
strv_t *argv_to_cmdline(strv_t *args) {
  if (!args)
    return NULL;
  strv_t *out = calloc(2, sizeof(strv_t));
  if (!out) {
    strv_destroy(args);
    return NULL;
  }
  out[0] = *args; /* ownership transfer: the table itself becomes element 0 */
  return out;     /* out[1] zeroed by calloc = the empty strv_t sentinel */
}

char *shorten_path(const char *root, const char *path) {
  if (!path)
    return NULL;
  if (root && *root && os_path_is_under(path, root)) {
    char *rel = os_path_rel_under(path, root);
    if (rel)
      return rel; /* inside the root: relative path */
  }
  return forge_strdup(path); /* outside the root or failed: verbatim */
}

int strv_append_args(strv_t *args, const char *text) {
  if (!args || !text)
    return -1;
  int count = 0;
  char **pieces = os_line2argv(text, &count);
  if (!pieces)
    return -1;
  int rc = 0;
  for (int i = 0; i < count && rc == 0; i++) {
    if (strv_append(args, pieces[i]) != 0)
      rc = -1;
  }
  for (int i = 0; i < count; i++)
    free(pieces[i]);
  free(pieces);
  return rc;
}