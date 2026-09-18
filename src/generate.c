/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file generate.c
 * @brief Implementation of gen_compile()/gen_link().
 *
 * External compilers are invoked as subprocesses via forge_os.h's
 * os_execute_raw(); command lines are assembled per the GCC / CLANG /
 * MSVC styles; see include/generate.h.
 */
#include "generate.h"
#include "forge_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int count_args(char **argv) {
  int n = 0;
  if (!argv)
    return 0;
  while (argv[n])
    n++;
  return n;
}

static const char *base_name(const char *path) {
  const char *sep = strrchr(path, '/');
  const char *sep2 = strrchr(path, '\\');
  if (sep2 > sep)
    sep = sep2;
  return sep ? sep + 1 : path;
}

static char *object_name(const char *target, int compiler) {
  const char *base = base_name(target);
  const char *dot = strrchr(base, '.');
  size_t name_len = dot ? (size_t)(dot - base) : strlen(base);
  const char *ext = compiler == MSVC ? ".obj" : ".o";
  char *name = malloc(name_len + strlen(ext) + 1);
  if (!name)
    return NULL;
  memcpy(name, base, name_len);
  strcpy(name + name_len, ext);
  return name;
}

static char *object_path(const char *target, const char *output_dir,
                         int compiler) {
  char *name = object_name(target, compiler);
  if (!name)
    return NULL;
  size_t dir_len = output_dir ? strlen(output_dir) : 0;
  int need_sep =
      dir_len > 0 && output_dir[dir_len - 1] != '/' && output_dir[dir_len - 1] != '\\';
  size_t len = dir_len + (size_t)need_sep + strlen(name) + 1;
  char *path = malloc(len);
  if (!path) {
    free(name);
    return NULL;
  }
  if (dir_len > 0) {
    memcpy(path, output_dir, dir_len);
    if (need_sep)
      path[dir_len++] = '/';
  }
  strcpy(path + dir_len, name);
  free(name);
  return path;
}

static int run_compile(char *compiler_path, int compiler, char *target,
                       char *output_dir, char **extra) {
  char *obj = object_path(target, output_dir, compiler);
  if (!obj)
    return -1;
  char *objflag = NULL;
  if (compiler == MSVC) {
    size_t flen = strlen(obj) + 4;
    objflag = malloc(flen);
    if (!objflag) {
      free(obj);
      return -1;
    }
    snprintf(objflag, flen, "/Fo%s", obj);
  }

  int extra_n = count_args(extra);
  size_t slots = (compiler == MSVC ? 5 : 6) + (size_t)extra_n;
  char **cmd = calloc(slots, sizeof(char *));
  if (!cmd) {
    free(objflag);
    free(obj);
    return -1;
  }
  int i = 0;
  cmd[i++] = compiler_path;
  for (int j = 0; j < extra_n; j++)
    cmd[i++] = extra[j];
  if (compiler == MSVC) {
    cmd[i++] = "/c";
    cmd[i++] = target;
    cmd[i++] = objflag;
  } else {
    cmd[i++] = "-c";
    cmd[i++] = target;
    cmd[i++] = "-o";
    cmd[i++] = obj;
  }
  cmd[i] = NULL;

  int ret = os_execute_raw(compiler_path, cmd, NULL);
  free(cmd);
  free(objflag);
  free(obj);
  return ret;
}

static int run_link(char *compiler_path, int compiler, char **targets,
                    char *output_file, char **extra) {
  char *outflag = NULL;
  if (compiler == MSVC) {
    size_t flen = strlen(output_file) + 4;
    outflag = malloc(flen);
    if (!outflag)
      return -1;
    snprintf(outflag, flen, "/Fe%s", output_file);
  }

  int extra_n = count_args(extra);
  int target_n = count_args(targets);
  /* Fixed slots common to both branches: prog + (-o/outflag) + NULL */
  size_t slots = 4 + extra_n + target_n;
  char **cmd = calloc(slots, sizeof(char *));
  if (!cmd) {
    free(outflag);
    return -1;
  }
  int i = 0;
  cmd[i++] = compiler_path;
  for (int j = 0; j < extra_n; j++)
    cmd[i++] = extra[j];
  for (int j = 0; j < target_n; j++)
    cmd[i++] = targets[j];
  if (compiler == MSVC) {
    cmd[i++] = outflag;
  } else {
    cmd[i++] = "-o";
    cmd[i++] = output_file;
  }
  cmd[i] = NULL;

  int ret = os_execute_raw(compiler_path, cmd, NULL);
  free(cmd);
  free(outflag);
  return ret;
}

int gen_compile(char *compiler_path, int compiler, char **targets,
                char *output_dir, char **argv) {
  if (!compiler_path || !targets || !*targets || !output_dir || !*output_dir)
    return -1;
  if (compiler != GCC && compiler != CLANG && compiler != MSVC)
    return -1;
  for (char **t = targets; *t; t++) {
    int ret = run_compile(compiler_path, compiler, *t, output_dir, argv);
    if (ret != 0)
      return ret;
  }
  return 0;
}

int gen_link(char *compiler_path, int compiler, char **targets,
             char *output_file, char **argv) {
  if (!compiler_path || !targets || !*targets || !output_file || !*output_file)
    return -1;
  if (compiler != GCC && compiler != CLANG && compiler != MSVC)
    return -1;
  return run_link(compiler_path, compiler, targets, output_file, argv);
}