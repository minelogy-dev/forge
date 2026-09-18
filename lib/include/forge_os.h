/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file forge_os.h
 * @brief Cross-platform OS abstraction layer of libforge: path,
 *        filesystem, and process operations.
 *
 * Keeps the same semantics as the host forge's include/os.h: every
 * function has both a POSIX (Linux/macOS) and a Windows implementation,
 * and each function's doc notes the underlying system call / API
 * (@note platform implementation notes):
 * - @b POSIX note: the corresponding POSIX syscall, e.g. stat(2),
 *   opendir(3);
 * - @b Windows note: the corresponding Win32 API, e.g. FindFirstFileW().
 *
 * Implementations live in lib/src/os/, compiled in by the build system:
 * - lib/src/os/unix.c   pure POSIX implementation (shared by the three
 *   platforms; no Apple branch);
 * - lib/src/os/apple.c  Apple (macOS) variant: os_exe_dir / os_now_ms /
 *   os_mtime_ms / os_export_flag;
 * - lib/src/os/linux.c  non-Apple POSIX os_exe_dir (Linux reads
 *   /proc/self/exe, FreeBSD/DragonFly use sysctl);
 * - lib/src/os/windows.c Windows implementation;
 * - lib/src/os/path.c   pure string path functions, shared by the three
 *   platforms.
 *
 * Note: apple.c / windows.c are currently stubs (disabled since
 * 2026-09-13, untested; see the header comments of each file) - they
 * provide no symbols on the pre-release Linux-only platform and fail
 * fast at link time; the macOS/Windows @note in the functions below is
 * only an ABI reminder, with no implementation behind it today.
 *
 * Memory and return-value conventions:
 * - path functions returning char* / char** return malloc-allocated
 *   memory on success, which the caller must free; NULL on failure;
 * - operation functions returning int: 0 on success, -1 on failure;
 * - predicate functions returning int: 1 if true, 0 if false;
 * - property functions returning uint64_t: 0 on failure.
 */
#ifndef FORGE_OS_H
#define FORGE_OS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------ platform detection notes ------------------------ */
#if defined(_WIN32) || defined(_WIN64)
/** @def FORGE_OS_WINDOWS Windows target-platform marker (value = 1) */
#define FORGE_OS_WINDOWS 1
#elif defined(__unix__) || defined(_unix) || defined(__APPLE__) || defined(__MACH__)
/** @def FORGE_OS_POSIX POSIX target-platform marker (value = 1, */
/**                       covers Linux and macOS) */
#define FORGE_OS_POSIX 1
#else
#error "forge: unsupported platform (only POSIX and Windows are supported)"
#endif

/* MSVC's C mode (/std:c11 and up) does not declare the POSIX name
   strdup; _strdup is always available. We map it with a macro to avoid
   C4996/implicit-declaration errors. strdup is widely used in
   lib/src/{build,type,compiler}.c, src/forge.c, tests/sub/test_args.c. */
#if defined(_MSC_VER)
#define strdup _strdup
#endif

/**
 * @brief Portable string copy (malloc-allocated; NULL on failure).
 *
 * strdup entered ISO C in C23; under older strict dialects (e.g.
 * gcc/clang `-std=c11|c17`) glibc and macOS do not declare strdup,
 * which triggers an implicit-declaration error (MSVC is covered by the
 * strdup->_strdup shim above). libforge uses this function internally
 * so it does not depend on extension visibility; it is equivalent to
 * strdup, including the NULL input -> NULL result contract.
 *
 * @param[in] s source string; may be NULL.
 * @return malloc-allocated copy; the caller must free it; NULL if s is
 *         NULL or on OOM.
 */
static inline char *forge_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s) + 1;
  char *d = (char *)malloc(n);
  if (!d)
    return NULL;
  memcpy(d, s, n);
  return d;
}

/**
 * @def FORGE_TLS
 * @brief Thread-local storage qualifier.
 *
 * Expands to `_declspec(thread)` under MSVC (cl and not clang-cl,
 * i.e. `_MSC_VER && !__clang__`), and to C11's `_Thread_local` for
 * all other compilers (GCC / Clang / clang-cl). Used for errno-style
 * thread-global state such as `os_verbose`: each thread has its own
 * copy, so a value set on the main thread via `os_set_verbose()` does
 * not leak into compile worker threads.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#define FORGE_TLS _declspec(thread)
#else
#define FORGE_TLS _Thread_local
#endif

/**
 * @brief Runtime output level (errno-style thread-global, three states).
 *
 * - 1 (-v): fully verbose - every executed command and its captured
 *   output is printed;
 * - 0 (default): every executed command is always printed (path line +
 *   command line); captured output is attached only when the command
 *   fails;
 * - -1 (-q): quiet - no output when commands do not fail, and an error
 *   block when they do.
 *
 * Consumed by build.c's compile/link execution passes: they use these
 * three states to decide on printing and quiet echo (see print_result
 * in build.c). It does not apply directly to the execution-layer API -
 * os_execute_raw() always passes through and the capture family always
 * captures, regardless of this value.
 * Declared FORGE_TLS: set/read by the calling thread via
 * os_set_verbose(). Defined in lib/src/args.c.
 */
extern FORGE_TLS int os_verbose;

/**
 * @brief Number of parallel compile tasks (plain global, not
 *        thread-local).
 *
 * Range: >= 1. Affects two places:
 * - build.c's compile pass runs N-way in parallel (N = min(os_jobs,
 *   number of files to compile));
 * - the link pass: for gcc/clang with LTO enabled and N > 1,
 *   `-flto` is rewritten to `-flto=N`; for MSVC with N > 1,
 *   `/CGTHREADS:N` is appended (cap 8).
 * Set by os_set_jobs(); defined in lib/src/args.c.
 */
extern int os_jobs;

/**
 * @brief Set the output level (three states, passed through).
 * @param[in] verbose -1 = -q quiet; 0 = default; 1 = -v fully verbose.
 */
void os_set_verbose(int verbose);

/**
 * @brief Set the number of parallel compile tasks.
 * @param[in] jobs the desired number of tasks; values below 1 are
 *            treated as 1.
 */
void os_set_jobs(int jobs);

/**
 * @brief Monotonic clock in milliseconds.
 *
 * @note platform implementation notes:
 * - @b POSIX: clock_gettime(2) + CLOCK_MONOTONIC;
 * - @b POSIX macOS: mach_absolute_time() + mach_timebase_info(),
 *   converted to milliseconds (clock_gettime needs macOS >= 10.12;
 *   mach_absolute_time works on all versions);
 * - @b Windows: GetTickCount64().
 *
 * Independent of the wall clock (not affected by system time
 * adjustments); only for elapsed-time measurements.
 * @return monotonic millisecond counter; 0 on failure.
 */
uint64_t os_now_ms(void);

/**
 * @brief Return the directory containing the current process's
 *        executable.
 *
 * @note platform implementation notes:
 * - @b POSIX: readlink(2) on /proc/self/exe, then take the directory
 *   part; macOS uses _NSGetExecutablePath(); FreeBSD/DragonFly use
 *   sysctl(KERN_PROC_PATHNAME);
 * - @b Windows: GetModuleFileNameW(), then take the directory part.
 *
 * build.c uses its return value as the "project root" to compute each
 * file's relative display path; when it cannot be obtained, it falls
 * back to the startup working directory (os_path_abs(NULL)) - this
 * only affects display, not the build.
 *
 * @return malloc-allocated absolute directory path; the caller must
 *         free it; NULL on failure.
 */
char *os_exe_dir(void);

/**
 * @brief Return the linker arguments needed to export dynamic symbols
 *        when linking an executable.
 *
 * Used by the host forge (generate_build) and libforge (link_script)
 * when linking artifacts, so the runtime can look up function_* symbols
 * in its own process via dlsym()/GetProcAddress().
 *
 * @note platform implementation notes:
 * - @b POSIX Linux: "-rdynamic";
 * - @b POSIX macOS: "-Wl,-export_dynamic";
 * - @b Windows:     "-Wl,--export-all-symbols" (GCC/CLANG; MSVC instead
 *   collects symbols one by one with /EXPORT:).
 *
 * @return static string; the caller must not modify or free it.
 */
const char *os_export_flag(void);

/**
 * @brief Return the executable file suffix.
 *
 * @note platform implementation notes:
 * - @b Windows: ".exe";
 * - @b POSIX: "".
 *
 * @return static string; the caller must not modify or free it.
 */
const char *os_exe_ext(void);

/**
 * @brief Cross-platform mutex object (pthread_mutex_t /
 *        CRITICAL_SECTION internally).
 *
 * The storage is sized for the worst requirement of both platforms (at
 * least the 64 bytes and 8-byte alignment the two implementations
 * need), so it can safely be declared by value as a local variable or
 * an array element. Must only be used via os_mutex_init() /
 * os_mutex_lock() / os_mutex_unlock(): it must not be locked/unlocked
 * before init, and must not be copied or rewritten byte by byte (_data
 * holds implementation internals).
 */
typedef struct os_mutex {
  union {
    void *p;      /**< placeholder: ensures pointer-level alignment */
    long long ll; /**< placeholder: ensures 8-byte alignment */
  } _align;
  unsigned char _data[64]; /**< platform mutex primitive storage (do not */
                           /**< access directly) */
} os_mutex;

/**
 * @brief Initialize a mutex.
 * @note platform implementation notes:
 * - @b POSIX: pthread_mutex_init(3);
 * - @b Windows: InitializeCriticalSection().
 * @param[in] m the mutex object.
 */
void os_mutex_init(os_mutex *m);

/**
 * @brief Lock (blocks until available).
 * @note platform implementation notes: @b POSIX: pthread_mutex_lock(3);
 * - @b Windows: EnterCriticalSection().
 * @param[in] m an initialized mutex object.
 */
void os_mutex_lock(os_mutex *m);

/**
 * @brief Unlock.
 * @note platform implementation notes:
 * - @b POSIX: pthread_mutex_unlock(3);
 * - @b Windows: LeaveCriticalSection().
 * @param[in] m a locked mutex object.
 */
void os_mutex_unlock(os_mutex *m);

/**
 * @brief Cross-platform thread handle (pthread_t / HANDLE internally).
 *
 * Like os_mutex, it uses fixed storage (at least 16 bytes, pointer-level
 * alignment) and can be declared by value as array elements (e.g. N
 * workers as `os_thread threads[N]`). Must only be used via
 * os_thread_start() / os_thread_join(); must not be copied or rewritten
 * byte by byte.
 */
typedef struct os_thread {
  union {
    void *p;      /**< placeholder: ensures pointer-level alignment */
    long long ll; /**< placeholder: ensures 8-byte alignment */
  } _align;
  unsigned char _data[16]; /**< platform thread handle storage (do not */
                           /**< access directly) */
} os_thread;

/**
 * @brief Start a thread that executes fn(arg).
 *
 * @note platform implementation notes:
 * - @b POSIX: pthread_create(3);
 * - @b Windows: CreateThread().
 *
 * @param[in] t   the thread handle (in the unused state).
 * @param[in] fn  the thread entry function (returns void, takes one
 *                void* argument).
 * @param[in] arg the argument passed to fn.
 * @return 0 on success, -1 on failure.
 */
int os_thread_start(os_thread *t, void (*fn)(void *), void *arg);

/**
 * @brief Wait for the thread to finish.
 * @note platform implementation notes:
 * - @b POSIX: pthread_join(3);
 * - @b Windows: WaitForSingleObject() + CloseHandle().
 * @param[in] t the handle of a started thread.
 */
void os_thread_join(os_thread *t);

/**
 * @brief Cross-platform condition variable (pthread_cond_t /
 *        CONDITION_VARIABLE internally).
 *
 * Uses the same fixed-capacity storage as os_mutex (at least 64 bytes,
 * pointer-level alignment) and can safely be declared by value as a
 * local variable or array element. Used together with os_mutex:
 * os_cond_wait() atomically releases the associated mutex and blocks,
 * then re-acquires it when woken; os_cond_signal() wakes one waiter,
 * os_cond_broadcast() wakes all. Must only be used via the os_cond_*
 * functions; must not be copied or rewritten byte by byte.
 *
 * Platform implementation notes:
 * - @b POSIX: pthread_cond_init(3) / pthread_cond_wait(3) /
 *   pthread_cond_signal(3) / pthread_cond_broadcast(3);
 * - @b Windows: InitializeConditionVariable() /
 *   SleepConditionVariableCS() / WakeConditionVariable() /
 *   WakeAllConditionVariable() (os_mutex is a CRITICAL_SECTION, and
 *   SleepConditionVariableCS pairs with it naturally).
 */
typedef struct os_cond {
  union {
    void *p;      /**< placeholder: ensures pointer-level alignment */
    long long ll; /**< placeholder: ensures 8-byte alignment */
  } _align;
  unsigned char _data[64]; /**< platform condition variable storage (do */
                           /**< not access directly) */
} os_cond;

/**
 * @brief Initialize a condition variable.
 * @param[in] c the condition variable object.
 */
void os_cond_init(os_cond *c);

/**
 * @brief Atomically release the associated mutex and block; re-acquire
 *        the mutex when woken or when returning.
 *
 * The caller must already hold m. Spurious wakeups are allowed; the
 * caller must re-check the wait condition in a while loop.
 *
 * @param[in] c an initialized condition variable object.
 * @param[in] m a locked mutex object (pthread_mutex_t* on POSIX,
 *              CRITICAL_SECTION* on Windows; alignment is forced by
 *              the fixed-capacity storage and converted per platform
 *              internally).
 */
void os_cond_wait(os_cond *c, os_mutex *m);

/**
 * @brief Wake a single waiter (no-op when none is waiting).
 * @param[in] c an initialized condition variable object.
 */
void os_cond_signal(os_cond *c);

/**
 * @brief Wake all waiters (no-op when none is waiting).
 * @param[in] c an initialized condition variable object.
 */
void os_cond_broadcast(os_cond *c);

/**
 * @brief Join two path segments.
 *
 * Joins with POSIX pathjoin() semantics: only one separator remains at
 * the join - trailing '/' or '\\' of a and leading ones of b are
 * stripped, then joined with a single '/'. Roots and drive roots (e.g.
 * "/", "C:\") are kept as is and do not participate in tail stripping
 * or separator insertion. If a or b is empty (including NULL), the
 * result is the other side verbatim; if b consists of separators only,
 * the result equals a with its trailing separator removed plus a single
 * '/'. No normalization is done beyond the join (".", ".." and interior
 * duplicate separators are preserved verbatim); a Windows drive
 * absolute path used as a suffix (e.g. "C:\foo") does not start with a
 * separator and is joined verbatim like an ordinary suffix.
 *
 * @param[in] a the path prefix; may be NULL (treated as an empty
 *              string).
 * @param[in] b the path suffix; may be NULL (treated as an empty
 *              string).
 * @return malloc-allocated joined result; the caller must free it;
 *         NULL on failure.
 */
char *os_path_join(const char *a, const char *b);

/**
 * @brief Return the directory part of a path.
 *
 * First strips the trailing run of '/' or '\\' (except for roots), then
 * truncates before the last separator:
 * - "a/b/c.txt" -> "a/b"; "a/b/" -> "a"; "/a" -> "/";
 * - returns "." when there is no separator (e.g. "a" -> ".").
 *
 * @param[in] path the path; may be NULL (treated as an empty string;
 *                 result is ".").
 * @return malloc-allocated result; the caller must free it; NULL on
 *         failure.
 */
char *os_path_dirname(const char *path);

/**
 * @brief Return the file name part of a path (the last path segment).
 *
 * First strips the trailing run of '/' or '\\' (except for roots), then
 * takes everything after the last separator:
 * - "a/b/c.txt" -> "c.txt"; "a/b/" -> "b"; "/" -> "/"; "a" -> "a".
 *
 * @param[in] path the path; may be NULL (treated as an empty string;
 *                 result is "").
 * @return malloc-allocated result; the caller must free it; NULL on
 *         failure.
 */
char *os_path_basename(const char *path);

/**
 * @brief Convert a path to an absolute path.
 *
 * If already absolute, copied verbatim; a relative path is joined with
 * the current working directory; NULL or an empty string returns the
 * current working directory itself. Performs "." / ".." normalization.
 *
 * @param[in] path a relative or absolute path; may be NULL.
 * @return malloc-allocated absolute path; the caller must free it;
 *         NULL on failure.
 */
char *os_path_abs(const char *path);

/**
 * @brief Collapse "." / ".." in a path and normalize separators.
 *
 * Pure string function; does not touch the filesystem; identical
 * behavior on all three platforms:
 * - folds "." segments and resolvable ".." segments ("a/b/../c" ->
 *   "a/c");
 * - consecutive separators collapse to one; trailing separators are
 *   removed;
 * - root prefixes are preserved verbatim and cannot be crossed by
 *   "..": POSIX root "/", drive root "C:/" (input may use "C:\\"), UNC
 *   "//server/share" (server and share count as part of the root);
 * - in relative paths, ".." that would go above the start is kept as a
 *   literal segment ("../x" -> "../x"); in absolute paths, ".." that
 *   would go above the root is discarded ("/.." -> "/");
 * - NULL input returns NULL; an empty collapsed result (e.g. ".",
 *   "a/..") returns the empty string "".
 *
 * Used by os_path_abs() to normalize its joined result; also public so
 * it can be unit-tested and reused.
 *
 * @param[in] path the path; may be NULL.
 * @return malloc-allocated result; the caller must free it; NULL if
 *         path is NULL.
 */
char *os_path_normalize(const char *path);

/**
 * @brief Determine whether path a lies under path b.
 *
 * First normalizes via os_path_abs() (collapses "." / "..", unifies
 * separators, converts to absolute), then does a pure string prefix
 * comparison:
 * - a equal to b counts as true; when b is a root (e.g. "/", "C:/"),
 *   every path under it is true;
 * - when b is NULL or an empty string, the current working directory
 *   is used;
 * - returns 0 when a is NULL, normalization fails, or a is not under
 *   b;
 * - symbolic links are not resolved: a path that reaches b through a
 *   link is compared literally and may be judged false.
 *
 * @param[in] a the path to test (file or directory), may be NULL
 *              (always returns 0).
 * @param[in] b the base directory path; may be NULL (uses the current
 *              working directory).
 * @return 1 if a lies under b (including equality), 0 otherwise.
 */
int os_path_is_under(const char *a, const char *b);

/**
 * @brief Return the path of a relative to b; NULL when a is not under
 *        b.
 *
 * Same normalization and test as os_path_is_under(); when true and the
 * relative part is non-empty, returns the relative path with separators
 * preserved (not flattened):
 * - "/a/b/c" relative to "/a/b" -> "c"; "/a/b/c/d" relative to "/a" ->
 *   "b/c/d"; "/a/x/../b" relative to "/a" -> "b";
 * - a equal to b (empty relative part), a not under b, a NULL, or
 *   failure -> NULL.
 *
 * When b is NULL or an empty string, the current working directory is
 * used. The return value, passed to os_path_join(b, result), recovers
 * the normalized form of a.
 *
 * @param[in] a the path whose relative path is wanted; may be NULL
 *              (returns NULL).
 * @param[in] b the base directory path; may be NULL (uses the current
 *              working directory).
 * @return malloc-allocated result; the caller must free it; NULL on
 *         failure or when a is not under b.
 */
char *os_path_rel_under(const char *a, const char *b);

/**
 * @brief Flatten a path into a name with no directory separators
 *        (injective encoding).
 *
 * First normalizes via os_path_abs() (relative paths become absolute
 * against the current working directory), then replaces separators in
 * the path with '_' and escapes literal '_' and '%':
 * - '/' and '\\' -> '_';
 * - literal '_' -> "%5F";
 * - literal '%' -> "%25".
 * The encoding is injective (different paths encode differently) and
 * reversible; it gives out-of-project source files a directory-free
 * landing spot under the output directory.
 *
 * @param[in] path the path; may be NULL (returns NULL).
 * @return malloc-allocated result; the caller must free it; NULL on
 *         failure.
 */
char *os_path_flatten(const char *path);

/**
 * @brief Compute a source file's relative landing spot in the output
 *        layout (without the output base directory).
 *
 * - File under the current working directory: mirrors the relative
 *   directory, separators preserved, e.g. "src/build/main.c" with ext
 *   ".o" yields "src/build/main.o";
 * - file outside it: flattens the absolute path via os_path_flatten(),
 *   e.g. "/usr/x/y.c" with ext ".o" yields "_usr_x_y.o".
 *
 * When ext is non-empty, the last '.' in the base name and everything
 * after it (not crossing a separator) are removed and ext is appended;
 * when ext is NULL or an empty string, the original file name (with
 * extension) is kept (for copy scenarios).
 * Returns NULL when the file is the working directory itself (empty
 * relative landing spot) or normalization fails.
 *
 * @param[in] file the source file path (relative or absolute); may be
 *                 NULL (returns NULL).
 * @param[in] ext  the target extension (e.g. ".o"); may be NULL / empty
 *                 (keeps the original name).
 * @return malloc-allocated relative landing spot; the caller must free
 *         it; NULL on failure.
 */
char *os_path_file_rel(const char *file, const char *ext);

/**
 * @brief Determine whether a path exists (file or directory).
 *
 * The only "entry-level" existence predicate: it only cares whether the
 * path entry is occupied, not what it points to; a symlink pointing to
 * a non-existent target (dangling link) counts as "existing". By
 * contrast, os_path_is_dir() / os_path_is_file() are "resolution-level"
 * predicates (they follow symlinks and care about the type of the path's
 * target); the semantic difference between the two is documented in
 * each function.
 *
 * @note platform implementation notes:
 * - @b POSIX: lstat(2) (does not follow symlinks);
 * - @b Windows: CreateFileW() + FILE_FLAG_OPEN_REPARSE_POINT (does not
 *   follow reparse points: symlinks/junctions - including dangling
 *   ones - count as "entry exists", consistent with POSIX lstat
 *   semantics).
 *
 * @param[in] path the path; may be NULL.
 * @return 1 if it exists, 0 if not (or path is NULL).
 */
int os_path_exists(const char *path);

/**
 * @brief Determine whether a path exists and is a directory.
 *
 * "Resolution-level" predicate: it cares about what the path actually
 * points to - a symlink pointing to a directory counts as a directory
 * (returns 1); directory-walking consumers follow directory links into
 * their targets, and protection against link loops is the caller's
 * responsibility (unlike os_path_exists()'s lstat entry semantics).
 *
 * @note platform implementation notes:
 * - @b POSIX: S_ISDIR() on stat(2) (follows symlinks, takes the link
 *   target);
 * - @b Windows: FILE_ATTRIBUTE_DIRECTORY from GetFileAttributesW()
 *   (follows reparse points; behavior matches POSIX).
 *
 * @param[in] path the path; may be NULL.
 * @return 1 if it is a directory, 0 otherwise.
 */
int os_path_is_dir(const char *path);

/**
 * @brief Determine whether a path exists and is a regular file.
 *
 * "Resolution-level" predicate: it cares about what the path actually
 * points to - a symlink pointing to a file counts as a file (returns
 * 1); used for "the compiler will open this file" scenarios such as
 * deciding whether an obj/.d is ready for incremental builds (unlike
 * os_path_exists()'s lstat entry semantics).
 *
 * @note platform implementation notes:
 * - @b POSIX: S_ISREG() on stat(2) (follows symlinks, takes the link
 *   target);
 * - @b Windows: GetFileAttributesW() (no FILE_ATTRIBUTE_DIRECTORY bit;
 *   follows reparse points; behavior matches POSIX).
 *
 * @param[in] path the path; may be NULL.
 * @return 1 if it is a regular file, 0 otherwise.
 */
int os_path_is_file(const char *path);

/**
 * @brief Determine whether a path is absolute.
 *
 * @note platform implementation notes:
 * - @b POSIX: starts with '/';
 * - @b Windows: a drive-letter absolute path of the form "C:\\..." or
 *   "C:/...", or a "\\\\..." UNC path.
 *
 * @param[in] path the path; may be NULL.
 * @return 1 if absolute, 0 otherwise.
 */
int os_path_is_absolute(const char *path);

/**
 * @brief Recursively create a directory, creating missing parents
 *        automatically.
 *
 * Existing directories along the path are skipped; if the final path is
 * already a directory, it is treated as success.
 * Accepts '/' and (on Windows) '\\' as separators.
 *
 * @note platform implementation notes:
 * - @b POSIX: mkdir(2) level by level, mode 0755; on EEXIST, continue
 *   if the path is already a directory (via os_path_is_dir(); stat
 *   follows symlinks, so a middle/final segment that is a link to a
 *   directory counts as existing and continues; the criterion is
 *   "would mkdir fail", consistent with windows.c; links to files or
 *   dangling links are judged a failure);
 * - @b Windows: CreateDirectoryW() level by level; on
 *   ERROR_ALREADY_EXISTS, continue when the path is already a
 *   directory (GetFileAttributesW, follows reparse points).
 *
 * @note degenerate input differences: for paths made of separators
 *   only (e.g. "/" or "\\"), the POSIX side returns 0 because the root
 *   already exists (existing means success); the Windows side has
 *   nothing to create and uniformly returns -1. Consecutive and
 *   trailing separators are skipped/ignored on both sides.
 *
 * @param[in] path the directory path.
 * @return 0 on success, -1 on failure.
 */
int os_mkdir_r(const char *path);

/**
 * @brief Create a single directory (the parent must already exist).
 *
 * @note platform implementation notes:
 * - @b POSIX: mkdir(2), mode 0755;
 * - @b Windows: CreateDirectoryW().
 *
 * @param[in] path the directory path.
 * @return 0 on success, -1 on failure.
 */
int os_mkdir(const char *path);

/**
 * @brief List all entry names in a directory (without "." and "..").
 *
 * @note platform implementation notes:
 * - @b POSIX: opendir(3)/readdir(3)/closedir(3);
 * - @b Windows: FindFirstFileW()/FindNextFileW()/FindClose().
 *
 * @param[in] path the directory path; may be NULL (then directly
 *                 returns NULL).
 * @param[out] len receives the number of entries; may be NULL; 0 is
 *                 written on failure or when the directory is empty.
 * @return a NULL-terminated char* array, each element a strdup'd entry
 *         name; the caller frees each element and then the array
 *         itself; NULL on failure.
 */
char **os_listdir(const char *path, int *len);

/**
 * @brief Remove an empty directory.
 *
 * @note platform implementation notes:
 * - @b POSIX: rmdir(2);
 * - @b Windows: RemoveDirectoryW().
 *
 * @param[in] path the directory path; may be NULL (then returns -1).
 * @return 0 on success, -1 on failure.
 */
int os_remove_dir(const char *path);

/**
 * @brief Open a file (thin cross-platform wrapper; forwards directly to
 *        the standard library fopen(3)).
 *
 * @param[in] path the file path.
 * @param[in] mode the open mode, same as fopen(3).
 * @return the file stream pointer on success, NULL on failure.
 */
static inline FILE *os_fopen(const char *path, const char *mode) {
  return fopen(path, mode);
}

/**
 * @brief Close a file (thin cross-platform wrapper; forwards directly
 *        to the standard library fclose(3)).
 *
 * @param[in] stream the file stream returned by os_fopen().
 * @return 0 on success, EOF on failure.
 */
static inline int os_fclose(FILE *stream) { return fclose(stream); }

/**
 * @brief Get a file's last modification time.
 *
 * @note platform implementation notes:
 * - @b POSIX: st_mtime from stat(2) (follows symlinks, takes the link
 *   target - serving the incremental-build "the compiler will open
 *   this file" intent);
 * - @b Windows: ftLastWriteTime from GetFileAttributesExW() (FILETIME:
 *   1601 epoch, 100 ns units), converted to Unix epoch seconds.
 *
 * @param[in] path the file path.
 * @return the last modification time in Unix seconds; 0 on failure.
 */
uint64_t os_mtime(const char *path);

/**
 * @brief Get a file's size.
 *
 * @note platform implementation notes:
 * - @b POSIX: st_size from stat(2) (follows symlinks, takes the link
 *   target; same rationale as os_mtime);
 * - @b Windows: nFileSizeHigh/Low from GetFileAttributesExW().
 *
 * @param[in] path the file path.
 * @return the size in bytes; 0 on failure.
 */
uint64_t os_size(const char *path);

/**
 * @brief Copy file contents; an existing destination file is
 *        overwritten.
 *
 * When src and dst refer to the same file, idempotently returns 0
 * without modifying the file or losing contents; the Windows
 * implementation recognizes the same file via GetFileInformationByHandle
 * volume serial number + file index (covering the same path and
 * case-alias and hard links).
 *
 * @note platform implementation notes:
 * - @b POSIX: first compares dev+ino with stat(2) (follows symlinks, so
 *   links to the same file are also recognized as the same file); when
 *   equal, idempotently returns 0; otherwise copies with
 *   open(2)/read(2)/write(2), mode 0644 for the target;
 * - @b Windows: CreateFileW() + ReadFile()/WriteFile(); the target is
 *   opened with CREATE_ALWAYS.
 *
 * @param[in] src the source file path.
 * @param[in] dst the destination file path.
 * @return 0 on success, -1 on failure.
 */
int os_copy_file(const char *src, const char *dst);

/**
 * @brief Remove a regular file.
 *
 * @note platform implementation notes:
 * - @b POSIX: unlink(2);
 * - @b Windows: DeleteFileW().
 *
 * @param[in] path the file path; may be NULL (then returns -1).
 * @return 0 on success, -1 on failure.
 */
int os_remove_file(const char *path);

/**
 * @brief Remove a file or directory (type is detected automatically;
 *        the directory must be empty).
 *
 * A symlink/junction pointing to a file or directory removes only the
 * link itself and is not followed to its target.
 *
 * @note platform implementation notes:
 * - @b POSIX: lstat(2) to detect the type (does not follow symlinks),
 *   then unlink(2) or rmdir(2);
 * - @b Windows: type detection via get_link_attributes()
 *   (FILE_FLAG_OPEN_REPARSE_POINT); directories (including directory
 *   symlinks/junctions) go through RemoveDirectoryW(), everything else
 *   (including file symlinks) through DeleteFileW(); reparse points
 *   remove only the link, never followed to the target.
 *
 * @param[in] path the path; may be NULL (then returns -1).
 * @return 0 on success, -1 on failure.
 */
int os_remove(const char *path);

/**
 * @brief Recursively remove a file or an entire directory tree.
 *
 * @note platform implementation notes:
 * - @b POSIX: opendir(3)/readdir(3) recursive traversal +
 *   unlink(2)/rmdir(2);
 * - @b Windows: FindFirstFileW() recursive traversal +
 *   DeleteFileW()/RemoveDirectoryW(); reparse points (symlinks/
 *   junctions) remove only the link itself.
 *
 * @param[in] path a file or directory path.
 * @return 0 on success, -1 on failure.
 */
int os_remove_r(const char *path);


/**
 * @brief Join an argv array into a single command-line string.
 *
 * Escaping contract (identical across platforms, user convention): only
 * the two characters `"` and `\` are handled. An argument that is an
 * empty string or contains whitespace (space/tab), quotes, or
 * backslashes is wrapped as a whole in double quotes; inside the
 * quotes, encoding follows CRT run-aware rules: `"` escapes to `\"`;
 * backslashes are kept literally, but doubled when immediately in front
 * of a quote (a backslash run followed by an interior quote emits 2r
 * backslashes plus `\"`, making the total run odd - on parsing, pairs
 * halve and the quote is literal); a trailing backslash run of an
 * argument is padded to an even length so the closing quote parses as a
 * delimiter. The encoding guarantees idempotent recovery by
 * os_line2argv and is consistent for both sh -c and cmd /c semantics;
 * all other arguments are emitted verbatim. Arguments are separated by
 * a single space.
 *
 * Inverse of os_line2argv: os_line2argv(os_argv2line(a)) recovers the
 * original array.
 *
 * @param[in] argv the NULL-terminated argument array.
 * @return malloc-allocated command-line string; the caller must free
 *         it; NULL on failure.
 */
char *os_argv2line(char **argv);

/**
 * @brief Split a command-line string into an argv array (inverse of
 *        os_argv2line).
 *
 * Parsing rules (same as the CRT in msdn.md): whitespace (space/tab)
 * separates arguments; a double-quoted region is one argument; inside
 * the quotes `\"` is a literal double quote and `\\` a literal
 * backslash; a run of backslashes followed by a quote halves by pairs
 * when even and, when odd, halves then treats the quote literally;
 * backslashes outside quotes stay literal; an unterminated double quote
 * extends to the end of the line.
 * The input is not modified.
 *
 * @param[in] line the command-line string.
 * @param[out] count optional: receives the number of arguments (may be
 *                   NULL).
 * @return malloc-allocated NULL-terminated argv array (each element is
 *         separately malloc'd; the caller frees each element and then
 *         the array itself); NULL on failure.
 */
char **os_line2argv(const char *line, int *count);


/**
 * @brief Execute an external program and block until it exits;
 *        stdout/stderr always pass through to the current process.
 *
 * The child's stdout/stderr always go directly to the terminal/current
 * process standard handles (not captured, not discarded, not buffered),
 * regardless of os_verbose; for scenarios such as test tools whose
 * "output must always be visible" (the test's own PASSED/FAILED lines
 * must not disappear under quiet mode).
 *
 * @note platform implementation notes:
 * - @b POSIX: fork(2) + execvp(3) + waitpid(2); when execvp(3) fails,
 *   the child exits with 127; when terminated by a signal, returns
 *   128 + the signal number.
 * - @b Windows: CreateProcessW() + WaitForSingleObject() +
 *   GetExitCodeProcess() (no pipe redirection; the child inherits the
 *   standard handles naturally).
 *
 * @param[in] target the program path to execute.
 * @param[in] argv   the NULL-terminated argument array; argv[0] is the
 *                   program name.
 * @param[in] workdir the child's working directory; NULL = inherit the
 *                    current cwd, non-NULL chdirs via a spawn file
 *                    action (no shell involved).
 * @return the child's exit code; -1 on process creation/launch
 *         failure.
 */
int os_execute_raw(char *target, char **argv, const char *workdir);

/**
 * @brief Get a file's last modification time with Unix epoch
 *        millisecond precision.
 *
 * @note platform implementation notes:
 * - @b POSIX: st_mtim (Linux) / st_mtimespec (macOS) from stat(2)
 *   (seconds + nanoseconds, converted to milliseconds; follows
 *   symlinks, takes the link target; same rationale as os_mtime);
 * - @b Windows: ftLastWriteTime from GetFileAttributesExW() (FILETIME:
 *   1601 epoch, 100 ns units), converted to Unix epoch milliseconds.
 *
 * Unlike os_mtime()'s second precision, this function serves the
 * strict `>` comparison of incremental builds, with about 1 ms
 * precision (consecutive modifications within the same millisecond may
 * be indistinguishable; a known limitation).
 *
 * @param[in] path the file path.
 * @return the last modification time in Unix epoch milliseconds; 0 on
 *         failure (NULL path, stat failure, etc.).
 */
uint64_t os_mtime_ms(const char *path);

/**
 * @brief Execute an external program and capture its stdout, returning
 *        a malloc-allocated string.
 *
 * No shell involved; argv semantics (fork/execvp/pipe on POSIX,
 * CreatePipe + CreateProcessW redirection on Windows).
 * Capture semantics are like a shell pipe `|`:
 * - when stdin is NULL, no process is created and nothing runs;
 *   directly returns NULL (so a multi-stage chain A|B|C short-circuits
 *   through when an intermediate stage fails);
 * - when stdin is non-NULL, its contents are written to the child's
 *   standard input (pass an empty string "", not NULL, for empty
 *   input);
 * - the return value is the child's complete stdout (malloc'd; the
 *   caller must free it); stderr passes through to the terminal;
 * - NULL on process creation / read-write failure; a non-zero child
 *   exit code is not a failure (same as shell `|`).
 *
 * @note known limitation: the parent writes all of stdin before reading
 * stdout, so the amount of data exchanged in one direction is limited
 * by the pipe buffer (about 64KB on POSIX, about 4KB on Windows);
 * exceeding it may deadlock; this library's uses (version probing,
 * small toolchains) stay well below the limit.
 *
 * @param[in] target the program path to execute.
 * @param[in] argv   the NULL-terminated argument array; argv[0] is the
 *                   program name.
 * @param[in] stdin  the contents written to the child's standard input;
 *                   NULL short-circuits without executing.
 * @return the child's stdout (malloc'd); NULL on failure or when stdin
 *         is NULL.
 */
char *os_execute_capture(char *target, char **argv, const char *stdin,
                          const char *workdir);

/**
 * @brief Same as os_execute_capture(), but captures stdout and stderr
 *        merged.
 *
 * Like shell `2>&1`: the child's stderr no longer passes through to the
 * terminal, but is written to the same capture pipe as stdout; for
 * cases like cl /Bv where the version banner lands on stderr.
 * All other semantics (including the stdin-NULL short-circuit and
 * non-zero exit code not being a failure) are identical to
 * os_execute_capture().
 *
 * @param[in] target the program path to execute.
 * @param[in] argv   the NULL-terminated argument array; argv[0] is the
 *                   program name.
 * @param[in] stdin  the contents written to the child's standard input;
 *                   NULL short-circuits without executing.
 * @return the child's merged stdout and stderr (malloc'd); NULL on
 *         failure or when stdin is NULL.
 */
char *os_execute_capture_all(char *target, char **argv, const char *stdin,
                              const char *workdir);

/**
 * @brief Same as os_execute_capture_all(), additionally reporting the
 *        child's exit status.
 *
 * For the compile pass's parallel workers: obtains the exit code while
 * merging captures (conventions shared with os_execute_raw: 127 =
 * exec failure, 128 + signal number = killed by a signal, -1 = process
 * creation/launch failure), avoiding double execution or missing exit
 * codes. With stdin NULL it likewise short-circuits without executing
 * (returns NULL and sets *status to -1).
 *
 * @param[in] target the program path to execute.
 * @param[in] argv   the NULL-terminated argument array; argv[0] is the
 *                   program name.
 * @param[in] stdin  the contents written to the child's standard input;
 *                   NULL short-circuits without executing.
 * @param[out] status the child's exit code (may be NULL; pass NULL
 *                   when not interested).
 * @return the child's merged stdout and stderr (malloc'd); NULL on
 *         failure or when stdin is NULL.
 */
char *os_execute_capture_all_status(char *target, char **argv,
                                    const char *stdin,
                                    const char *workdir, int *status);

/**
 * @brief Execute argv via the system shell and return the concatenation
 *        of stderr and stdout.
 *
 * Takes argv, converts it to a command line via os_argv2line internally,
 * and hands it to the system shell (no wrapper that accepts a raw
 * cmdline argument). Platform differences are encapsulated in the
 * platform implementation files per the os/ routing convention:
 * - @b POSIX: `sh -c <line>` (the fixed system default shell, no
 *   override);
 * - @b Windows: `cmd.exe /D /S /C ""<line>""` (fixed, no override;
 *   /S plus double-quote wrapping keeps the line's own quotes from
 *   being stripped by the outer layer).
 *
 * Output contract: stderr and stdout are each captured through their
 * own pipe and then concatenated; in the returned string the stderr
 * part comes first and the stdout part last (not interleaved). The
 * child's exit code is not reflected in the return value (when sh is
 * missing the program, stderr is visible).
 *
 * @param[in] argv the NULL-terminated argument array.
 * @return malloc-allocated stderr+stdout concatenated string; the
 *         caller must free it; NULL on process creation failure.
 */
char *os_shell(char **argv);

#endif
