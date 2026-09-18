/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file unix.c
 * @brief The pure POSIX implementation of forge_os.h.
 *
 * Target platforms: all POSIX systems. The whole file is wrapped in
 * #if defined(FORGE_OS_POSIX), so it produces no symbols when
 * compiled on Windows. The Apple-specific variants (os_exe_dir /
 * os_now_ms / os_mtime_ms / os_export_flag) moved to apple.c, and
 * the non-Apple POSIX os_exe_dir lives in linux.c; this file no
 * longer references any non-POSIX API. The documented behavior of
 * each function is in forge_os.h.
 */
#define _POSIX_C_SOURCE 200809L

#define _GNU_SOURCE /* posix_spawn_file_actions_addchdir_np (glibc GNU ext.) */
#define _POSIX_C_SOURCE 200809L

#include "forge_os.h"
#include <spawn.h>
#include <unistd.h>

#if defined(FORGE_OS_POSIX)

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Already-absolute paths are normalized and returned directly;
   relative paths / NULL / the empty string are joined on a getcwd
   base before normalizing (NULL and the empty string return the
   cwd itself). The getcwd buffer starts at 256 and doubles on
   ERANGE (overflow-safe; cap beyond SIZE_MAX/2 counts as failure).
   Edge case: after the cwd's directory is deleted, getcwd() may
   return a relative path (POSIX allows it); no absolute path can
   be built then, so NULL is returned */
char *os_path_abs(const char *path) {
  if (path && os_path_is_absolute(path))
    return os_path_normalize(path);
  const char *rel = (path && *path) ? path : "";
  size_t cap = 256;
  for (;;) {
    char *buf = malloc(cap);
    if (!buf)
      return NULL;
    if (getcwd(buf, cap)) {
      if (!os_path_is_absolute(buf)) {
        free(buf);
        return NULL;
      }
      char *joined = os_path_join(buf, rel);
      free(buf);
      if (!joined)
        return NULL;
      char *out = os_path_normalize(joined);
      free(joined);
      return out;
    }
    free(buf);
    if (errno != ERANGE || cap > (size_t)-1 / 2)
      return NULL;
    cap *= 2;
  }
}

/* Predicate: 1 when the path entry exists (a file, a directory or
   a symlink all count); does not follow symlinks (lstat), so a
   dangling link counts as "exists"; path NULL or a failed lstat
   returns 0 */
int os_path_exists(const char *path) {
  if (!path)
    return 0;
  struct stat st;
  return lstat(path, &st) == 0;
}

/* Predicate: 1 when the path exists and is a directory; follows
   symlinks (stat, the link target -- callers care about "what it
   points at" rather than the entry itself, e.g. directory traversal
   and recursive mkdir, hence the difference from the lstat entry
   semantics of os_path_exists); path NULL, a failed stat or a
   non-directory returns 0 */
int os_path_is_dir(const char *path) {
  if (!path)
    return 0;
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Predicate: 1 when the path exists and is a regular file; follows
   symlinks (stat, the link target, for the same reason as
   os_path_is_dir, e.g. deciding whether an obj/.d is ready for an
   incremental build); path NULL, a failed stat or a non-regular
   file returns 0 */
int os_path_is_file(const char *path) {
  if (!path)
    return 0;
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Recursive creation: duplicate the path, truncate at each
   separator and mkdir level by level; continue only when EEXIST
   and the path is already a directory (os_path_is_dir, stat
   follows symlinks: a link to a directory counts as existing);
   any other EEXIST is an error. Consecutive separators are
   skipped and trailing ones do not matter; the last segment shares
   the "exists means success" semantics of the intermediate levels,
   avoiding false positives from the TOCTOU race between stat and
   mkdir. A pure-separator path (e.g. "/") returns 0 because it
   already exists; the difference from windows.c, which always
   returns -1 for degenerate input, is documented in forge_os.h */
int os_mkdir_r(const char *path) {
  if (!path || !*path)
    return -1;
  size_t len = strlen(path);
  char *buf = malloc(len + 1);
  if (!buf)
    return -1;
  memcpy(buf, path, len + 1);

  int ret = 0;
  for (size_t i = 1; i < len; i++) {
    if (buf[i] != '/' || buf[i - 1] == '/')
      continue;
    buf[i] = '\0';
    if (mkdir(buf, 0755) != 0 &&
        !(errno == EEXIST && os_path_is_dir(buf))) {
      ret = -1;
      goto out;
    }
    buf[i] = '/';
  }

  if (!(os_path_is_dir(buf) || mkdir(buf, 0755) == 0 ||
        (errno == EEXIST && os_path_is_dir(buf))))
    ret = -1;

out:
  free(buf);
  return ret;
}

int os_mkdir(const char *path) {
  if (!path || !*path)
    return -1;
  return mkdir(path, 0755) == 0 ? 0 : -1;
}

char **os_listdir(const char *path, int *len) {
  if (len)
    *len = 0;
  if (!path)
    return NULL;

  DIR *d = opendir(path);
  if (!d)
    return NULL;

  size_t cap = 16, count = 0;
  char **names = malloc(cap * sizeof(char *));
  if (!names) {
    closedir(d);
    return NULL;
  }

  int failed = 0;
  struct dirent *de;
  errno = 0;
  while ((de = readdir(d)) != NULL) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;
    if (count + 2 > cap) {
      size_t ncap = cap * 2;
      if (ncap <= cap || ncap > SIZE_MAX / sizeof(char *)) {
        failed = 1;
        break;
      }
      char **n = realloc(names, ncap * sizeof(char *));
      if (!n) {
        failed = 1;
        break;
      }
      names = n;
      cap = ncap;
    }
    names[count] = strdup(de->d_name);
    if (!names[count]) {
      failed = 1;
      break;
    }
    count++;
  }
  /* a failed readdir returns NULL and sets errno; that tells it
   apart from a normal end of traversal */
  if (!failed && errno)
    failed = 1;
  closedir(d);

  if (failed) {
    for (size_t i = 0; i < count; i++)
      free(names[i]);
    free(names);
    return NULL;
  }
  names[count] = NULL;
  if (len)
    *len = (int)count;
  return names;
}

/* remove an empty directory (rmdir requires it to be empty);
   NULL input returns -1 */
int os_remove_dir(const char *path) {
  if (!path)
    return -1;
  return rmdir(path) == 0 ? 0 : -1;
}

/* Attribute: the last-modification Unix seconds of the file;
   follows symlinks (stat -- serving the incremental-build intent
   that "the compiler will open that file"; the timestamp is by
   definition that of the link target); path NULL or a failed stat
   returns 0 */
uint64_t os_mtime(const char *path) {
  if (!path)
    return 0;
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  return (uint64_t)st.st_mtime;
}

/* Attribute: the size of the file in bytes; follows symlinks
   (stat, the link target, same reason as os_mtime); path NULL or a
   failed stat returns 0 */
uint64_t os_size(const char *path) {
  if (!path)
    return 0;
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  return (uint64_t)st.st_size;
}

int os_copy_file(const char *src, const char *dst) {
  if (!src || !dst)
    return -1;
  /* First compare dev+ino via stat: when src and dst are the same
     file (the same path, a hard link, or symlinks to the same
     file), return 0 idempotently, so O_TRUNC never clears the
     source first and loses data; stat follows symlinks precisely
     to recognize links "pointing at the same target" (unlike the
     lstat entry semantics of the predicate family) */
  struct stat ss, ds;
  if (stat(src, &ss) != 0)
    return -1;
  if (stat(dst, &ds) == 0 && ss.st_dev == ds.st_dev && ss.st_ino == ds.st_ino)
    return 0;
  int in = open(src, O_RDONLY);
  if (in < 0)
    return -1;
  int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) {
    close(in);
    return -1;
  }

  char buf[16384];
  int ret = 0;
  for (;;) {
    ssize_t n = read(in, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR)
        continue;
      ret = -1;
      break;
    }
    if (n == 0)
      break;
    ssize_t off = 0;
    while (off < n) {
      ssize_t w = write(out, buf + off, (size_t)(n - off));
      if (w <= 0) {
        if (w < 0 && errno == EINTR)
          continue;
        ret = -1;
        break;
      }
      off += w;
    }
    if (ret != 0)
      break;
  }

  close(out);
  close(in);
  return ret;
}

/* remove a regular file or a link; NULL input returns -1 */
int os_remove_file(const char *path) {
  if (!path)
    return -1;
  return unlink(path) == 0 ? 0 : -1;
}

/* Remove a file or directory: type-check with lstat (no symlink
   following; a link removes only the link itself), directories go
   through rmdir (must be empty), the rest through unlink; NULL or
   a failed lstat returns -1 */
int os_remove(const char *path) {
  if (!path)
    return -1;
  struct stat st;
  if (lstat(path, &st) != 0)
    return -1;
  if (S_ISDIR(st.st_mode))
    return os_remove_dir(path);
  return os_remove_file(path);
}

/* Directory-fd-based recursive removal: every classification and
   operation goes through openat/unlinkat relative to the parent fd
   without following symlinks (O_NOFOLLOW), avoiding the TOCTOU
   window between lstat and opendir that could delete a link target
   by mistake; when openat fails with ELOOP/ENOTDIR (a symlink or a
   regular file), fall back to unlinkat to remove the entry itself */
#define OS_REMOVE_R_MAX_DEPTH 512

static int remove_recursive_at(int parent_fd, const char *name, int depth) {
  int fd = openat(parent_fd, name,
                  O_RDONLY | O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    if (errno == ENOENT)
      return -1;
    int r;
    do {
      r = unlinkat(parent_fd, name, 0);
    } while (r != 0 && errno == EINTR);
    return r == 0 ? 0 : -1;
  }
  DIR *d = fdopendir(fd);
  if (!d) {
    close(fd);
    return -1;
  }
  if (depth >= OS_REMOVE_R_MAX_DEPTH) {
    /* Depth guard: at the limit, stop recursing; the whole subtree
       stays and the upper rmdir failure counts as -1, keeping an
       extremely deep tree from exhausting the call stack */
    closedir(d);
    return -1;
  }
  int ret = 0;
  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;
    if (remove_recursive_at(dirfd(d), de->d_name, depth + 1) != 0)
      ret = -1;
  }
  if (closedir(d) != 0)
    ret = -1;
  int r;
  do {
    r = unlinkat(parent_fd, name, AT_REMOVEDIR);
  } while (r != 0 && errno == EINTR);
  if (r != 0)
    ret = -1;
  return ret;
}

/* os_remove_r implementation: best effort -- a failed child never
   blocks the others; any failure returns -1; symlinks delete only
   the link itself */
int os_remove_r(const char *path) { return remove_recursive_at(AT_FDCWD, path, 0); }

static char *capture_impl(char *target, char **argv, const char *stdin_data,
                          const char *workdir, int with_stderr, int *status);

/* Uniform posix_spawn launch: the file_actions do the optional
   chdir(workdir) and the fd redirections (in_fd->0, out_fd->1,
   out_fd->2 when with_stderr; negative = no redirection). Returns
   0 on success (pid valid) or an error code; ENOSYS = the
   platform has no addchdir_np and workdir is non-empty. */
static int spawn_exec(char *target, char **argv, const char *workdir,
                      int in_fd, int out_fd, int with_stderr, pid_t *pid) {
  if (!target || !argv || !*argv || !pid)
    return -1;
  posix_spawn_file_actions_t fa;
  if (posix_spawn_file_actions_init(&fa) != 0)
    return -1;
  int err = 0;
#if defined(__GLIBC__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__) || defined(__APPLE__) || defined(__MACH__)
  if (workdir && *workdir) {
    err = posix_spawn_file_actions_addchdir_np(&fa, workdir);
    if (err)
      goto out;
  }
#else
  if (workdir && *workdir) {
    err = ENOSYS; /* no addchdir_np on this platform: refuse instead
                     of silently inheriting the cwd */
    goto out;
  }
#endif
  if (in_fd >= 0 &&
      (err = posix_spawn_file_actions_adddup2(&fa, in_fd, STDIN_FILENO)))
    goto out;
  if (out_fd >= 0 &&
      (err = posix_spawn_file_actions_adddup2(&fa, out_fd, STDOUT_FILENO)))
    goto out;
  if (with_stderr && out_fd >= 0 &&
      (err = posix_spawn_file_actions_adddup2(&fa, out_fd, STDERR_FILENO)))
    goto out;
  extern char **environ;
  err = posix_spawnp(pid, target, &fa, NULL, argv, environ);
out:
  posix_spawn_file_actions_destroy(&fa);
  return err; /* 0 = success */
}

/* Pass-through execution: posix_spawn + waitpid; the child's
   stdout/stderr inherit the current fds (no redirection); a failed
   spawn (including exec not starting) returns 127, a signal
   termination 128 + the signal number */
int os_execute_raw(char *target, char **argv, const char *workdir) {
  pid_t pid;
  int err = spawn_exec(target, argv, workdir, -1, -1, 0, &pid);
  if (err != 0)
    return err == ENOSYS ? -1 : 127;
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR)
      return -1;
  }
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return -1;
}

#if !(defined(__APPLE__) || defined(__MACH__))
/* Millisecond mtime: follows symlinks (stat, the link target, same
   reason as os_mtime); path NULL or a failed stat returns 0.
   Platforms inside the support matrix
   (Linux/FreeBSD/OpenBSD/DragonFly) use the nanosecond precision of
   st_mtim; out-of-matrix platforms without st_mtim (AIX/Solaris
   etc.) fall back to the second-level st_mtime, matching the
   precision of os_mtime() (corresponding to st_mtimespec in
   apple.c) */
uint64_t os_mtime_ms(const char *path) {
  if (!path)
    return 0;
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  uint64_t sec, nsec;
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
  sec = (uint64_t)st.st_mtim.tv_sec;
  nsec = (uint64_t)st.st_mtim.tv_nsec;
#else
  /* Solaris 11+ actually provides st_mtim, but it is outside the
     support matrix; uniformly fall back to the second-level field,
     so any POSIX platform compiles */
  sec = (uint64_t)st.st_mtime;
  nsec = 0;
#endif
  return sec * 1000ULL + nsec / 1000000ULL;
}
#endif /* !(__APPLE__ || __MACH__) */

/* Capture implementation: write stdin first, then collect stdout
   (the merged variant dup2s stderr to the same write end). No
   shell involved; process-creation failure / read failure / OOM
   return NULL, and a nonzero child exit code is not a failure
   (same as a shell `|`); when status is non-NULL it receives the
   child's exit status, by convention: -1 = process
   creation/launch failure, 127 = exec failure, 128+signal =
   signal termination.
   A stdin write failure only stops the writing, not the whole
   capture: when the child exits early its read end closes and
   write returns EPIPE (SIGPIPE is globally ignored in args.c, so
   this process is not terminated); the output the child already
   produced is still collected and the child is reaped after that. */
static char *capture_impl(char *target, char **argv, const char *stdin_data,
                          const char *workdir, int with_stderr, int *status) {
  if (status)
    *status = -1;
  if (!stdin_data)
    return NULL;

  int inpipe[2], outpipe[2];
  if (pipe(inpipe) != 0)
    return NULL;
  if (pipe(outpipe) != 0) {
    close(inpipe[0]);
    close(inpipe[1]);
    return NULL;
  }

  pid_t pid;
  int err = spawn_exec(target, argv, workdir, inpipe[0], outpipe[1],
                       with_stderr, &pid);
  if (err != 0) {
    close(inpipe[0]);
    close(inpipe[1]);
    close(outpipe[0]);
    close(outpipe[1]);
    if (status)
      *status = err == ENOSYS ? -1 : 127;
    return NULL;
  }

  close(inpipe[0]);
  close(outpipe[1]);

  /* Write stdin first, then collect output, so the child cannot
     deadlock on pipe capacity; a single exchange larger than the
     pipe buffer (~64KB) can still deadlock -- a known limitation
     noted in forge_os.h */
  size_t inlen = strlen(stdin_data);
  size_t off = 0;
  while (off < inlen) {
    ssize_t w = write(inpipe[1], stdin_data + off, inlen - off);
    if (w <= 0) {
      /* Bail on w<=0 (EINTR retries; EPIPE etc. mean the child closed
         its read end early -- stop writing; output is still
         collected and the child reaped afterwards) */
      if (w < 0 && errno == EINTR)
        continue;
      break;
    }
    off += (size_t)w;
  }
  close(inpipe[1]);

  size_t cap = 256, len = 0;
  char *buf = malloc(cap);
  if (buf) {
    for (;;) {
      char tmp[4096];
      ssize_t n = read(outpipe[0], tmp, sizeof(tmp));
      if (n < 0) {
        if (errno == EINTR)
          continue;
        free(buf);
        buf = NULL;
        break;
      }
      if (n == 0)
        break;
      if (len + (size_t)n + 1 > cap) {
        /* geometric growth (1 byte reserved for the terminator);
           failure is cleaned up as OOM */
        size_t ncap = cap;
        while (ncap < len + (size_t)n + 1)
          ncap *= 2;
        char *nb = realloc(buf, ncap);
        if (!nb) {
          free(buf);
          buf = NULL;
          break;
        }
        buf = nb;
        cap = ncap;
      }
      memcpy(buf + len, tmp, (size_t)n);
      len += (size_t)n;
    }
  }
  close(outpipe[0]);

  int st = 0;
  int werr = 0;
  while (waitpid(pid, &st, 0) < 0) {
    if (errno != EINTR) {
      werr = 1;
      break;
    }
  }
  if (status) {
    if (werr || (!WIFEXITED(st) && !WIFSIGNALED(st)))
      *status = -1;
    else if (WIFEXITED(st))
      *status = WEXITSTATUS(st);
    else
      *status = 128 + WTERMSIG(st);
  }

  if (buf) {
    /* the read loop guarantees cap >= len + 1 at all times; just add
    the terminator */
    buf[len] = '\0';
  }
  return buf;
}

char *os_execute_capture(char *target, char **argv, const char *stdin_data,
                          const char *workdir) {
  return capture_impl(target, argv, stdin_data, workdir, 0, NULL);
}

char *os_execute_capture_all(char *target, char **argv, const char *stdin_data,
                              const char *workdir) {
  return capture_impl(target, argv, stdin_data, workdir, 1, NULL);
}

char *os_execute_capture_all_status(char *target, char **argv,
                                    const char *stdin_data,
                                    const char *workdir, int *status) {
  return capture_impl(target, argv, stdin_data, workdir, 1, status);
}

/* ------------------------ mutexes and threads ------------------------ */

/* The thread argument is heap-allocated and freed inside the
   thread by the trampoline: this avoids the race where the caller
   returns before the thread starts and the argument is
   invalidated (the argument is copied by value into the heap,
   independent of any caller stack object) */
struct thread_arg {
  void (*fn)(void *);
  void *arg;
};

static void *thread_tramp(void *p) {
  struct thread_arg *ta = p;
  ta->fn(ta->arg);
  free(ta);
  return NULL;
}

/* Default-attribute mutex: pthread_mutex_init can only fail on
   misuse (EINVAL) or out of memory, the latter unreachable on a
   normal system; the os_mutex_init contract is void, so the
   return value is not checked */
void os_mutex_init(os_mutex *m) {
  _Static_assert(sizeof(os_mutex) >= sizeof(pthread_mutex_t),
                 "os_mutex storage is too small for pthread_mutex_t");
  pthread_mutex_init((pthread_mutex_t *)m, NULL);
}

/* Lock/unlock failures only happen on misuse (uninitialized, an
   unlock without holding the lock, etc.), a caller-side
   violation; both contracts are void, so the return values are
   not checked */
void os_mutex_lock(os_mutex *m) { pthread_mutex_lock((pthread_mutex_t *)m); }

void os_mutex_unlock(os_mutex *m) { pthread_mutex_unlock((pthread_mutex_t *)m); }

/* Condition variable: init/signal/broadcast/wait have no return
   value, or failures only happen on misuse (uninitialized, wait
   without holding the lock, etc.); all contracts are void, so the
   return values are not checked. wait automatically releases the
   associated mutex (the built-in condition-variable convention) */
void os_cond_init(os_cond *c) {
  _Static_assert(sizeof(os_cond) >= sizeof(pthread_cond_t),
                 "os_cond storage is too small for pthread_cond_t");
  pthread_cond_init((pthread_cond_t *)c, NULL);
}

void os_cond_wait(os_cond *c, os_mutex *m) {
  pthread_cond_wait((pthread_cond_t *)c, (pthread_mutex_t *)m);
}

void os_cond_signal(os_cond *c) { pthread_cond_signal((pthread_cond_t *)c); }

void os_cond_broadcast(os_cond *c) { pthread_cond_broadcast((pthread_cond_t *)c); }

int os_thread_start(os_thread *t, void (*fn)(void *), void *arg) {
  _Static_assert(sizeof(os_thread) >= sizeof(pthread_t),
                 "os_thread storage is too small for pthread_t");
  struct thread_arg *ta = malloc(sizeof(struct thread_arg));
  if (!ta)
    return -1;
  ta->fn = fn;
  ta->arg = arg;
  pthread_t th;
  if (pthread_create(&th, NULL, thread_tramp, ta) != 0) {
    free(ta);
    return -1;
  }
  /* Copy the whole os_thread as opaque storage: the assert
     guarantees pthread_t fits in the whole struct, whereas the 16
     bytes of _data alone may not be enough */
  memcpy(t, &th, sizeof(th));
  return 0;
}

/* pthread_join returns an error only on misuse (EINVAL: invalid
   handle / double join; ESRCH: no such thread) or self-join
   (EDEADLK); the os_thread_join contract is void, and the caller
   guarantees correct use per the contract, so the return value is
   not checked */
void os_thread_join(os_thread *t) {
  pthread_t th;
  memcpy(&th, t, sizeof(th));
  pthread_join(th, NULL);
}

/* --------------------- clock and executable directory --------------------- */

#if !(defined(__APPLE__) || defined(__MACH__))
/* Monotonic clock in milliseconds (CLOCK_MONOTONIC, unaffected by
   system time adjustments); a failed clock_gettime returns 0 */
uint64_t os_now_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0;
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* the flag needed to export dynamic symbols at link time
   (-rdynamic on Linux; the macOS variant lives in apple.c) */
const char *os_export_flag(void) { return "-rdynamic"; }
#endif /* !(__APPLE__ || __MACH__) */

/* executable file extension: none on POSIX, so an empty string is
   returned (the Windows variant lives in windows.c) */
const char *os_exe_ext(void) { return ""; }

/* --------------------- shell execution (os_shell) --------------------- */

/* capture buffer (same hand-rolled style as capture_impl:
   geometric growth, a terminator slot at the end) */
typedef struct {
  char *buf;
  size_t len, cap;
} shell_buf;

/* append data; -1 on failure (the old buffer is kept; the caller
   does the uniform cleanup) */
static int shell_buf_append(shell_buf *b, const char *data, size_t n) {
  if (b->len + n + 1 > b->cap) {
    size_t ncap = b->cap ? b->cap : 256;
    while (ncap < b->len + n + 1)
      ncap *= 2;
    char *nb = realloc(b->buf, ncap);
    if (!nb)
      return -1;
    b->buf = nb;
    b->cap = ncap;
  }
  memcpy(b->buf + b->len, data, n);
  b->len += n;
  return 0;
}

/* os_shell: runs `sh -c <line>` and returns the stderr+stdout
   concatenation (err first, then out, not interleaved). line is
   produced by os_argv2line under the cross-platform contract of
   "only \" and \\ are special". The two pipes are captured
   separately and polled together against a deadlock: neither pipe
   deadlocking when it is full while the other is unread. Read
   failure / OOM: the part already read is kept and the whole
   result is still returned (best effort); a failed fork or a
   failed line generation returns NULL. */
char *os_shell(char **argv) {
  if (!argv)
    return NULL;

  char *line = os_argv2line(argv);
  if (!line)
    return NULL;
  char *shell_argv[] = {(char *)"sh", (char *)"-c", line, NULL};

  int errpipe[2] = {-1, -1}, outpipe[2] = {-1, -1};
  if (pipe(errpipe) != 0 || pipe(outpipe) != 0)
    goto fail;

  pid_t pid = fork();
  if (pid < 0)
    goto fail;

  if (pid == 0) {
    /* child: both write ends go to stderr/stdout; a failed exec
       exits with 127 */
    dup2(errpipe[1], STDERR_FILENO);
    dup2(outpipe[1], STDOUT_FILENO);
    close(errpipe[0]);
    close(errpipe[1]);
    close(outpipe[0]);
    close(outpipe[1]);
    execvp("sh", shell_argv);
    _exit(127);
  }

  /* parent: close the write ends, read both pipes to EOF */
  close(errpipe[1]);
  close(outpipe[1]);

  shell_buf err = {0}, out = {0};
  int err_open = 1, out_open = 1;
  while (err_open || out_open) {
    struct pollfd fds[2];
    nfds_t nfds = 0;
    int err_idx = -1;
    if (err_open) {
      err_idx = (int)nfds;
      fds[nfds].fd = errpipe[0];
      fds[nfds].events = POLLIN;
      nfds++;
    }
    if (out_open) {
      fds[nfds].fd = outpipe[0];
      fds[nfds].events = POLLIN;
      nfds++;
    }
    int r = poll(fds, nfds, -1);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      break; /* poll failed: stop the loop, keep what was read */
    }
    for (nfds_t i = 0; i < nfds; i++) {
      int closed = 0;
      if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
        char tmp[4096];
        ssize_t n = read(fds[i].fd, tmp, sizeof(tmp));
        if (n > 0) {
          shell_buf *sb = (int)i == err_idx ? &err : &out;
          shell_buf_append(sb, tmp, (size_t)n); /* OOM ignored: best effort */
        } else if (n == 0) {
          closed = 1;
        } else if (errno != EINTR && errno != EAGAIN) {
          closed = 1;
        }
      }
      if (closed) {
        close(fds[i].fd);
        if ((int)i == err_idx)
          err_open = 0;
        else
          out_open = 0;
      }
    }
  }

  /* reap the child (pipe EOF before waitpid: no deadlock risk in
     the same process) */
  while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}

  /* concatenate err + out */
  size_t elen = err.len, olen = out.len;
  char *res = malloc(elen + olen + 1);
  if (res) {
    if (elen)
      memcpy(res, err.buf, elen);
    if (olen)
      memcpy(res + elen, out.buf, olen);
    res[elen + olen] = '\0';
  }
  free(err.buf);
  free(out.buf);
  free(line);
  return res;

fail:
  if (errpipe[0] >= 0) {
    close(errpipe[0]);
    close(errpipe[1]);
  }
  if (outpipe[0] >= 0) {
    close(outpipe[0]);
    close(outpipe[1]);
  }
  free(line);
  return NULL;
}

#endif /* FORGE_OS_POSIX */
