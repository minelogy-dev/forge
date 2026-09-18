/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(_linux) || defined(linux)
int os = 0;
#elif defined(_WIN32) || defined(_WIN64)
int os = 1;
#elif defined(__APPLE__) || defined(__MACH__)
int os = 2;
#else
int os = 3;
#endif

static char *dup_n(const char *s, size_t n) {
  char *out = malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

/* If the line containing the key match has a '#' between its start
   and the match, the match is inside a comment and is invalid (same
   rule as conf_defines in forge.c: '#' starts a comment cut off at
   the end of the line), preventing commented lines such as
   `# compiler=MSVC` from silently taking effect as configuration. */
static int hash_before_key(const char *p, const char *buf) {
  const char *q = p;
  while (q > buf && q[-1] != '\n')
    q--;
  for (; q < p; q++)
    if (*q == '#')
      return 1;
  return 0;
}

static char *get_value(char *buf, const char *key) {
  if (!buf || !key)
    return NULL;
  size_t klen = strlen(key);
  if (klen == 0) return NULL;
  char *p = buf;
  while ((p = strstr(p, key)) != NULL) {
    if (p[klen] == '=' && !hash_before_key(p, buf))
      break;
    p += klen;
  }
  if (!p || p[klen] != '=')
    return NULL;
  p += klen + 1;
  while (*p == ' ' || *p == '\t')
    p++;
  size_t len = strcspn(p, "\r\n");
  char *hash = memchr(p, '#', len); /* '#' starts comment; cut at line end */
  if (hash)
    len = (size_t)(hash - p);
  while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t'))
    len--;
  return dup_n(p, len);
}

static char *get_value_platform(char *buf, const char *base,
                                const char *suffix) {
  char key[64];
  snprintf(key, sizeof(key), "%s_%s", base, suffix);
  char *v = get_value(buf, key);
  if (v)
    return v;
  return get_value(buf, base);
}

char *get_compiler_path(char *buf) {
  switch (os) {
  case 0:
    return get_value_platform(buf, "compiler_path", "linux");
  case 1:
    return get_value_platform(buf, "compiler_path", "windows");
  case 2:
    return get_value_platform(buf, "compiler_path", "mac_os");
  default:
    return get_value(buf, "compiler_path");
  }
}

char *get_compiler(char *buf) {
  switch (os) {
  case 0:
    return get_value_platform(buf, "compiler", "linux");
  case 1:
    return get_value_platform(buf, "compiler", "windows");
  case 2:
    return get_value_platform(buf, "compiler", "mac_os");
  default:
    return get_value(buf, "compiler");
  }
}