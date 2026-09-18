/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file line.c
 * @brief os_argv2line / os_line2argv implementation: the quoting
 *        rules of argv <-> command-line strings.
 *
 * Pure platform-independent logic (no system calls). Together with
 * os_shell in the platform files os/{unix,windows}.c it follows the
 * "os/ directory routing" convention: every platform difference
 * lives in the platform file; only the two-way escape contract (a
 * user convention) stays here: just the two characters `"` and `\`
 * are special, designed after the CRT command-line parsing rules
 * documented in msdn.md, so the semantics match both sh -c and
 * cmd /c and os_line2argv(os_argv2line(a)) restores a idempotently.
 *
 * Self-consistency constraint: the os/ directory files are compiled
 * standalone by the host tool (-I include -I lib/include, without
 * lib/src), so this file must not depend on internal.h; it uses the
 * small growable buffer dbuf defined in this file.
 */
#include "forge_os.h"

#include <stdlib.h>
#include <string.h>

/* small growable buffer (a local implementation with the same
   semantics as strbuf) */
typedef struct {
  char *buf;
  size_t len, cap;
} dbuf;

static int dbuf_grow(dbuf *b, size_t need) {
  if (b->len + need + 1 <= b->cap)
    return 0;
  size_t ncap = b->cap ? b->cap : 64;
  while (ncap < b->len + need + 1)
    ncap *= 2;
  char *nb = realloc(b->buf, ncap);
  if (!nb)
    return -1;
  b->buf = nb;
  b->cap = ncap;
  return 0;
}

static int dbuf_appendn(dbuf *b, const char *s, size_t n) {
  if (dbuf_grow(b, n) != 0)
    return -1;
  memcpy(b->buf + b->len, s, n);
  b->len += n;
  b->buf[b->len] = '\0';
  return 0;
}

static int dbuf_char(dbuf *b, char c) { return dbuf_appendn(b, &c, 1); }

static int dbuf_str(dbuf *b, const char *s) {
  return dbuf_appendn(b, s, strlen(s));
}

static void dbuf_free(dbuf *b) {
  free(b->buf);
  b->buf = NULL;
  b->len = b->cap = 0;
}

/* whether the argument needs double quotes: empty, or containing
   whitespace / quotes / backslashes */
static int needs_quote(const char *arg) {
  if (*arg == '\0')
    return 1;
  for (const char *p = arg; *p; p++) {
    if (*p == ' ' || *p == '\t' || *p == '"' || *p == '\\')
      return 1;
  }
  return 0;
}

/* append `count` backslash characters */
static int dbuf_backslashes(dbuf *b, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (dbuf_char(b, '\\') != 0)
      return -1;
  }
  return 0;
}

char *os_argv2line(char **argv) {
  if (!argv)
    return NULL;
  dbuf b = {0};
  for (int i = 0; argv[i]; i++) {
    if (i > 0 && dbuf_char(&b, ' ') != 0)
      goto oom;
    const char *a = argv[i];
    int failed = 0;
    if (needs_quote(a)) {
      /* Exact CRT encoding: backslashes inside quotes stay literal,
         except right before a quote character (run-aware there: r
         backslashes + quote -> 2r backslashes + \", the odd total
         run 2r+1 parses as r literal backslashes + a literal
         quote); a trailing backslash run must be even (pad by one
         when odd) so the closing quote after it parses as a
         delimiter */
      failed = dbuf_char(&b, '"');
      const char *p = a;
      while (*p && !failed) {
        if (*p == '\\') {
          const char *q = p;
          while (*q == '\\')
            q++;
          size_t k = (size_t)(q - p);
          if (*q == '"') {
            if (dbuf_backslashes(&b, 2 * k) != 0 ||
                dbuf_appendn(&b, "\\\"", 2) != 0) {
              failed = 1;
              break;
            }
            p = q + 1;
          } else if (*q == '\0') {
            if (dbuf_backslashes(&b, k + (k % 2)) != 0) {
              failed = 1;
              break;
            }
            p = q;
          } else {
            if (dbuf_backslashes(&b, k) != 0) {
              failed = 1;
              break;
            }
            p = q;
          }
        } else if (*p == '"') {
          failed = dbuf_appendn(&b, "\\\"", 2);
          if (!failed)
            p++;
        } else {
          failed = dbuf_char(&b, *p);
          if (!failed)
            p++;
        }
      }
      if (!failed)
        failed = dbuf_char(&b, '"');
    } else {
      failed = dbuf_str(&b, a);
    }
    if (failed)
      goto oom;
  }
  if (!b.buf) {
    /* Empty argv: return a malloc'ed empty string to keep the
       contract (the caller always frees) */
    b.buf = malloc(1);
    if (!b.buf)
      return NULL;
    b.buf[0] = '\0';
  }
  return b.buf;
oom:
  dbuf_free(&b);
  return NULL;
}

/* free the allocated elements of argv and the array itself */
static void free_argv(char **argv, int argc) {
  if (!argv)
    return;
  for (int i = 0; i < argc; i++)
    free(argv[i]);
  free(argv);
}

char **os_line2argv(const char *line, int *count) {
  if (count)
    *count = 0;
  if (!line)
    return NULL;

  char **argv = NULL;
  int argc = 0, cap = 0;
  const char *p = line;

  for (;;) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '\0')
      break;

    dbuf b = {0};
    int failed = 0;
    int inq = 0;
    while (*p && !failed) {
      char c = *p;
      if (c == '"') {
        inq = !inq; /* the quote toggles the closed state (CRT has
                       no double-quote nesting) */
        p++;
        continue;
      }
      if (c == '\\') {
        /* Consecutive backslashes are digested by the CRT rules:
           before a '"', an even run halves pairwise and an odd run
           halves then escapes the quote as literal (no state
           toggle); not before a '"' or at the end of the string,
           all stay literal */
        const char *q = p;
        while (*q == '\\')
          q++;
        size_t k = (size_t)(q - p);
        if (*q == '"') {
          failed = dbuf_appendn(&b, p, k / 2) != 0;
          if (!failed && k % 2 == 1)
            failed = dbuf_char(&b, '"') != 0;
          if (!failed && k % 2 == 0)
            inq = !inq; /* even: the quote is a delimiter, toggle the state */
          if (failed)
            break;
          p = q + 1;
          continue;
        }
        /* not followed by a quote: keep everything literal (append
           char by char) */
        failed = dbuf_appendn(&b, p, k) != 0;
        if (failed)
          break;
        p = q;
        continue;
      }
      if (!inq && (c == ' ' || c == '\t'))
        break; /* whitespace outside quotes: the argument ends
                  here (not consumed) */
      if (dbuf_char(&b, c) != 0) {
        failed = 1;
        break;
      }
      p++;
    }
    if (failed) {
      dbuf_free(&b);
      free_argv(argv, argc);
      return NULL;
    }

    if (argc + 1 >= cap) { /* geometric growth (incl. NULL slot) */
      size_t ncap = cap ? (size_t)cap * 2 : 8;
      if (ncap < (size_t)argc + 2)
        ncap = (size_t)argc + 2;
      char **na = realloc(argv, ncap * sizeof(char *));
      if (!na) {
        dbuf_free(&b);
        free_argv(argv, argc);
        return NULL;
      }
      argv = na;
      cap = (int)ncap;
    }
    /* an empty argument segment (e.g. "") yields an empty string */
    argv[argc] = b.buf ? b.buf : forge_strdup("");
    if (!argv[argc]) {
      dbuf_free(&b);
      free_argv(argv, argc);
      return NULL;
    }
    b.buf = NULL; /* ownership moves to argv[argc], preventing a
                     double free in dbuf_free */
    dbuf_free(&b);
    argc++;
  }

  if (argc == 0) {
    argv = malloc(sizeof(char *)); /* empty line: NULL-terminator only */
    if (!argv)
      return NULL;
    argv[0] = NULL;
  } else {
    argv[argc] = NULL; /* growth reserved argc+1 slots */
  }
  if (count)
    *count = argc;
  return argv;
}