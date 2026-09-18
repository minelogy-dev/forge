/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file linux.c
 * @brief Non-Apple POSIX platform-specific implementation of
 *        forge_os.h: os_exe_dir.
 *
 * Moved out of unix.c; resolves the executable's directory:
 * - Linux: readlink(2) reads /proc/self/exe; the buffer starts at
 *   256 bytes and doubles, covering both the "exactly cap-1"
 *   (possibly truncated) and the ENAMETOOLONG (the kernel deems the
 *   buffer too small) edge cases; cap doubles only after a size_t
 *   overflow check; overflow gives up;
 * - FreeBSD/DragonFly: sysctl(KERN_PROC_PATHNAME), also doubling,
 *   covering both n == cap in the result (exactly at the upper
 *   bound, possibly truncated) and ENOMEM (buffer too small; the
 *   required length has been written back to n);
 * - other non-Apple POSIX: returns NULL (the call site falls back
 *   to cwd, so no symbol is missing at link time).
 *
 * The whole file is wrapped in #if defined(FORGE_OS_POSIX) &&
 * !(defined(__APPLE__) || defined(__MACH__)); the rest of the pure
 * POSIX functions live in unix.c.
 */
#define _POSIX_C_SOURCE 200809L

#include "forge_os.h"

#if defined(FORGE_OS_POSIX) && !(defined(__APPLE__) || defined(__MACH__))

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if defined(__linux__)
#include <unistd.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/param.h>
#include <sys/sysctl.h>
#endif

char *os_exe_dir(void) {
#if defined(__linux__)
  size_t cap = 256;
  char *buf;
  for (;;) {
    buf = malloc(cap);
    if (!buf)
      return NULL;
    ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
    if (n < 0) {
      /* ENAMETOOLONG: the kernel deems the buffer too small; double and
         retry. Other errors fail right away */
      if (errno != ENAMETOOLONG) {
        free(buf);
        return NULL;
      }
      free(buf);
      if (cap > SIZE_MAX / 2)
        return NULL;
      cap *= 2;
      continue;
    }
    /* strictly less than cap-1 means untruncated; exactly cap-1 counts
       as possibly truncated and doubles */
    if ((size_t)n < cap - 1) {
      buf[n] = '\0';
      break;
    }
    free(buf);
    if (cap > SIZE_MAX / 2)
      return NULL;
    cap *= 2;
  }
  /* take the directory part: only what precedes the last '/' */
  char *slash = strrchr(buf, '/');
  if (slash)
    *slash = '\0';
  return buf;
#elif defined(__FreeBSD__) || defined(__DragonFly__)
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
  size_t cap = 256;
  char *buf;
  for (;;) {
    buf = malloc(cap);
    if (!buf)
      return NULL;
    size_t n = cap;
    if (sysctl(mib, 4, buf, &n, NULL, 0) != 0) {
      /* ENOMEM: buffer too small (the needed length was written back to
         n); double and retry. Other failures */
      if (errno != ENOMEM) {
        free(buf);
        return NULL;
      }
      free(buf);
      if (cap > SIZE_MAX / 2)
        return NULL;
      cap *= 2;
      continue;
    }
    if (n < cap) {
      /* n may include the trailing NUL (FreeBSD) or not; guarantee
         termination either way */
      if (n == 0 || buf[n - 1] != '\0')
        buf[n] = '\0';
      break;
    }
    /* n == cap: exactly at the upper bound, possibly truncated;
       double and retry */
    free(buf);
    if (cap > SIZE_MAX / 2)
      return NULL;
    cap *= 2;
  }
  char *slash = strrchr(buf, '/');
  if (slash)
    *slash = '\0';
  return buf;
#else
  return NULL;
#endif
}

#endif /* FORGE_OS_POSIX && !(__APPLE__ || __MACH__) */
