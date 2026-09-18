/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file path.c
 * @brief Implementation of the pure-string path functions in
 *        forge_os.h (shared by all three platforms).
 *
 * os_path_join() / os_path_dirname() / os_path_basename() /
 * os_path_is_absolute() depend on no platform API, so they live in
 * their own file, avoiding a duplicated implementation in unix.c /
 * windows.c. The platform implementations of the other functions
 * are in lib/src/os/unix.c and lib/src/os/windows.c; the behavior
 * of each function is documented in forge_os.h.
 */
#include "forge_os.h"

#include <stdlib.h>
#include <string.h>

static int is_sep(char c) { return c == '/' || c == '\\'; }

static int is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* drive root "C:\" / "C:/" (letter + ':' + separator, exactly 3 long) */
static int is_drive_root(const char *p, size_t len) {
  return len == 3 && is_alpha(p[0]) && p[1] == ':' && is_sep(p[2]);
}

static char *dup_str(const char *s) {
  size_t len = strlen(s);
  char *out = malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, s, len + 1);
  return out;
}

char *os_path_join(const char *a, const char *b) {
  size_t alen = a ? strlen(a) : 0;
  size_t blen = b ? strlen(b) : 0;
  if (alen == 0)
    return dup_str(b ? b : "");
  if (blen == 0)
    return dup_str(a);

  /* POSIX pathjoin() semantics: only one separator stays at the
     joint -- strip the trailing separators of a and the leading
     ones of b, then join with a single '/'; the root (all
     separators) and the drive roots "C:\" / "C:/" are kept as-is
     and take no part in stripping or inserting */
  int a_is_root = is_drive_root(a, alen);
  size_t aend = alen;
  if (!a_is_root)
    while (aend > 1 && is_sep(a[aend - 1]))
      aend--;
  if (!a_is_root && aend == 1 && is_sep(a[0]))
    a_is_root = 1;
  size_t bstart = 0;
  while (bstart < blen && is_sep(b[bstart]))
    bstart++;

  size_t a_out = a_is_root ? (is_drive_root(a, alen) ? alen : 1) : aend;
  int junc = !a_is_root; /* a non-root prefix gets a single '/' appended */
  size_t blen2 = blen - bstart;
  size_t outlen = a_out + (size_t)junc + blen2;
  char *out = malloc(outlen + 1);
  if (!out)
    return NULL;
  memcpy(out, a, a_out);
  size_t o = a_out;
  if (junc)
    out[o++] = '/';
  if (blen2)
    memcpy(out + o, b + bstart, blen2);
  out[outlen] = '\0';
  return out;
}

char *os_path_dirname(const char *path) {
  if (!path)
    path = "";
  size_t len = strlen(path);
  if (is_drive_root(path, len))
    return dup_str(path); /* the drive root "C:\" / "C:/" is returned as-is */
  while (len > 1 && is_sep(path[len - 1]))
    len--;
  if (len == 1 && is_sep(path[0]))
    return dup_str(path); /* the root "/" stays as-is */
  size_t cut = len;
  while (cut > 0 && !is_sep(path[cut - 1]))
    cut--;
  if (cut == 0)
    return dup_str("."); /* no separator: dir part is the current dir */
  size_t plen = cut;
  while (plen > 1 && is_sep(path[plen - 1]))
    plen--; /* strip trailing separators from the prefix, keeping
               only the root's single '/' */
  if (plen == 2 && is_alpha(path[0]) && path[1] == ':' && is_sep(path[2]))
    plen = 3; /* the drive-root prefix "C:\" must not shrink to "C:" */
  char *out = malloc(plen + 1);
  if (!out)
    return NULL;
  memcpy(out, path, plen);
  out[plen] = '\0';
  return out;
}

char *os_path_basename(const char *path) {
  if (!path)
    path = "";
  size_t len = strlen(path);

  /* Root-prefix length: 3 for a drive "C:\", 1 for a path starting
     with a separator (incl. UNC and "/"), else 0. Trailing
     separators are never stripped below the root prefix, so a
     drive root never shrinks to "C:" */
  size_t root = 0;
  if (len >= 3 && is_alpha(path[0]) && path[1] == ':' && is_sep(path[2]))
    root = 3;
  else if (is_sep(path[0]))
    root = 1;

  /* strip trailing separators (the root and drive roots excepted):
     "a/b/" -> "a/b" */
  while (len > root && is_sep(path[len - 1]))
    len--;

  if (len == 0)
    return dup_str(path); /* the empty string is returned as-is */
  if (is_drive_root(path, len)) {
    char *out = malloc(4); /* the drive root "C:\" / "C:/" is returned as-is */
    if (!out)
      return NULL;
    memcpy(out, path, 3);
    out[3] = '\0';
    return out;
  }
  if (root == 1 && len == 1)
    return dup_str("/"); /* an all-separator string (e.g. "//",
                            "\\") normalizes to the root "/" */

  /* take the part after the last separator; with no separator, the
   whole string */
  size_t cut = len;
  while (cut > 0 && !is_sep(path[cut - 1]))
    cut--;
  size_t blen = len - cut;
  char *out = malloc(blen + 1);
  if (!out)
    return NULL;
  memcpy(out, path + cut, blen);
  out[blen] = '\0';
  return out;
}

int os_path_is_absolute(const char *path) {
  if (!path || !*path)
    return 0;
#if defined(FORGE_OS_WINDOWS)
  /* UNC / device paths ("\\server\share", "\\?\" etc.): the first
     two characters are both separators. Before reading path[1],
     the explicit path[1] != '\0' guarantees a length >= 2; a
     separator can never be '\0', so the guard does not change the
     semantics, it only makes the bounds protection explicit. */
  if (is_sep(path[0]) && path[1] != '\0' && is_sep(path[1]))
    return 1;
  /* Drive absolute paths "C:\..." / "C:/...": letter + ':' +
     separator; a drive-relative path "C:foo" has a non-separator
     third character, returning 0. Reading path[1] relies on
     is_alpha(path[0]) (a letter is never '\0', giving a length
     >= 1); reading path[2] relies on path[1] == ':' (never '\0',
     giving a length >= 2); both are explicit length guards. */
  if (is_alpha(path[0]) && path[1] == ':' && is_sep(path[2]))
    return 1;
  return 0;
#else
  /* POSIX: only a leading '/' is an absolute path */
  return path[0] == '/';
#endif
}

typedef struct {
  const char *s; /**< segment content (pointing into the input string) */
  size_t n;      /**< segment length */
} path_seg;

/* ".." segment test (only used by os_path_normalize) */
static int seg_is_dotdot(const path_seg *sg) {
  return sg->n == 2 && sg->s[0] == '.' && sg->s[1] == '.';
}

char *os_path_normalize(const char *path) {
  if (!path)
    return NULL;
  if (!*path)
    return dup_str("");

  /* Root prefix: always normalized to '/' separators, kept as-is and
     never crossed by `..`. Only exactly two leading separators
     count as UNC; three or more leading separators collapse to a
     single '/' (POSIX semantics, "///x" -> "/x") */
  char prefix[3];
  size_t prefix_len = 0;
  if (is_sep(path[0]) && is_sep(path[1]) && !is_sep(path[2])) {
    prefix[0] = '/';
    prefix[1] = '/';
    prefix_len = 2; /* UNC "\\server\share\..." */
  } else if (is_sep(path[0])) {
    prefix[0] = '/';
    prefix_len = 1; /* the POSIX root "/" */
  } else if (is_alpha(path[0]) && path[1] == ':' && is_sep(path[2])) {
    prefix[0] = path[0];
    prefix[1] = ':';
    prefix[2] = '/';
    prefix_len = 3; /* the drive root "C:\" / "C:/" */
  }
  int rel = prefix_len == 0; /* relative path (no root prefix) */

  /* collect all segments after the root prefix (empty segments from
   consecutive separators are skipped) */
  size_t len = strlen(path);
  size_t cap = 8, count = 0;
  path_seg *segs = malloc(cap * sizeof(path_seg));
  if (!segs)
    return NULL;

  size_t root_segs = 0; /* a UNC's server/share counts as root,
                           not pop-able by `..` */
  size_t i = prefix_len;
  while (i < len) {
    while (i < len && is_sep(path[i]))
      i++;
    if (i >= len)
      break;
    size_t start = i;
    while (i < len && !is_sep(path[i]))
      i++;
    size_t n = i - start;
    if (n == 1 && path[start] == '.')
      continue; /* fold "." */
    if (count + 1 > cap) {
      size_t ncap = cap * 2;
      path_seg *ns = realloc(segs, ncap * sizeof(path_seg));
      if (!ns) {
        free(segs);
        return NULL;
      }
      segs = ns;
      cap = ncap;
    }
    segs[count].s = path + start;
    segs[count].n = n;
    if (prefix_len == 2 && count < 2)
      root_segs = count + 1; /* server / share count as root */
    count++;
  }

  /* Fold "..": pop when the top is a normal segment (not ".." and
     above the root); a relative path keeps the literal ".." past
     the start, an absolute path drops it past the root */
  size_t out = 0;
  for (size_t j = 0; j < count; j++) {
    const path_seg *sg = &segs[j];
    if (seg_is_dotdot(sg)) {
      if (out > root_segs && !seg_is_dotdot(&segs[out - 1]))
        out--; /* pop the previously resolved segment */
      else if (rel) {
        segs[out].s = sg->s;
        segs[out].n = sg->n;
        out++;
      }
      continue;
    }
    segs[out].s = sg->s;
    segs[out].n = sg->n;
    out++;
  }
  count = out;

  /* rebuild: root prefix + segments joined by '/' (no duplicate
   separator when the prefix already ends with one) */
  size_t total = prefix_len;
  for (size_t j = 0; j < count; j++)
    total += 1 + segs[j].n;
  char *buf = malloc(total + 1);
  if (!buf) {
    free(segs);
    return NULL;
  }
  size_t o = 0;
  if (prefix_len) {
    memcpy(buf, prefix, prefix_len);
    o = prefix_len;
  }
  for (size_t j = 0; j < count; j++) {
    if (o > 0 && !is_sep(buf[o - 1]))
      buf[o++] = '/';
    memcpy(buf + o, segs[j].s, segs[j].n);
    o += segs[j].n;
  }
  buf[o] = '\0';
  free(segs);
  return buf;
}

/* ------------------- output layout (mirror / flatten) ------------------- */

/* a and b are both absolute paths normalized by os_path_abs();
   test whether a is under b (a == b included). When b ends with a
   separator (roots like "/", "C:/"), the prefix itself is the
   boundary and the next char may be any plain segment; otherwise a
   must have a separator right after the prefix, so "/a/bc" is not
   misjudged as being under "/a/b". */
static int under_norm(const char *a, const char *b) {
  size_t bl = strlen(b);
  if (bl == 0)
    return 0;
  if (strncmp(a, b, bl) != 0)
    return 0;
  if (a[bl] == '\0')
    return 1; /* a == b */
  if (is_sep(b[bl - 1]))
    return 1; /* b is a root ("/", "C:/") */
  return is_sep(a[bl]);
}

/* Encode the first n chars of s into dst under the flatten rules
   (capacity at least n * 3 + 1), returning the number of chars
   written. Rules: separator ('/' or '\\') -> '_', literal '_' ->
   "%5F", literal '%' -> "%25" (an injective encoding: different
   paths encode differently, and it is reversible) */
static size_t flatten_into(char *dst, const char *s, size_t n) {
  size_t k = 0;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (is_sep(c)) {
      dst[k++] = '_';
    } else if (c == '_') {
      dst[k++] = '%';
      dst[k++] = '5';
      dst[k++] = 'F';
    } else if (c == '%') {
      dst[k++] = '%';
      dst[k++] = '2';
      dst[k++] = '5';
    } else {
      dst[k++] = c;
    }
  }
  return k;
}

int os_path_is_under(const char *a, const char *b) {
  if (!a)
    return 0;
  char *na = os_path_abs(a);
  char *nb = os_path_abs(b); /* NULL / empty string -> cwd */
  if (!na || !nb) {
    free(na);
    free(nb);
    return 0;
  }
  int ret = under_norm(na, nb);
  free(na);
  free(nb);
  return ret;
}

char *os_path_rel_under(const char *a, const char *b) {
  if (!a)
    return NULL;
  char *na = os_path_abs(a);
  char *nb = os_path_abs(b);
  if (!na || !nb) {
    free(na);
    free(nb);
    return NULL;
  }
  char *out = NULL;
  if (under_norm(na, nb)) {
    const char *rel = na + strlen(nb);
    while (*rel == '/' || *rel == '\\')
      rel++;
    if (*rel)
      out = dup_str(rel); /* a == b (empty relative) uniformly becomes NULL */
  }
  free(na);
  free(nb);
  return out;
}

char *os_path_flatten(const char *path) {
  if (!path)
    return NULL;
  char *abs = os_path_abs(path);
  if (!abs)
    return NULL;
  size_t len = strlen(abs);
  char *out = malloc(len * 3 + 1); /* worst case: 3x the width */
  if (!out) {
    free(abs);
    return NULL;
  }
  size_t k = flatten_into(out, abs, len);
  out[k] = '\0';
  free(abs);
  return out;
}

char *os_path_file_rel(const char *file, const char *ext) {
  if (!file)
    return NULL;
  char *abs = os_path_abs(file);
  char *root = os_path_abs(NULL);
  if (!abs || !root) {
    free(abs);
    free(root);
    return NULL;
  }

  int inside = under_norm(abs, root);
  const char *src;
  if (inside) {
    const char *rel = abs + strlen(root);
    while (*rel == '/' || *rel == '\\')
      rel++;
    if (!*rel) { /* the file is root itself: no relative target,
                    failing like being outside the root */
      free(abs);
      free(root);
      return NULL;
    }
    src = rel;
  } else {
    src = abs;
  }

  const char *e = (ext && *ext) ? ext : NULL;

  /* when e is set, drop the last '.' in the base name and what
   follows it (never crossing a separator) */
  size_t len = strlen(src);
  size_t stem = len;
  if (e) {
    for (size_t i = len; i > 0; i--) {
      char c = src[i - 1];
      if (c == '.') {
        stem = i - 1;
        break;
      }
      if (is_sep(c))
        break;
    }
  }

  size_t extlen = e ? strlen(e) : 0;
  char *name = malloc(stem * 3 + extlen + 1); /* flattening worst case: 3x */
  if (!name) {
    free(abs);
    free(root);
    return NULL;
  }
  size_t k;
  if (inside) {
    memcpy(name, src, stem); /* inside the project: mirror the
                                relative dirs, separators kept */
    k = stem;
  } else {
    k = flatten_into(name, src, stem);
  }
  if (extlen)
    memcpy(name + k, e, extlen + 1);
  else
    name[k] = '\0';
  free(abs);
  free(root);
  return name;
}
