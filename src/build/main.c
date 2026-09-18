/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define SELF_HANDLE GetModuleHandle(NULL)
#define GET_FUNC(h, name) GetProcAddress((HMODULE)(h), name)
#define ERROR_MSG() "GetProcAddress failed"
#else
#include <dlfcn.h>
#define SELF_HANDLE dlopen(NULL, RTLD_LAZY)
#define GET_FUNC(h, name) dlsym((void *)(h), name)
#define ERROR_MSG() dlerror()
#endif

#if defined _WIN32
#define SYMBOL_PUBLIC _declspec(dllexport)
#else
#define SYMBOL_PUBLIC __attribute__((visibility("default")))
#endif

SYMBOL_PUBLIC int main(int argc, char *argv[]) {
  if (argc == 0) {
    // Never write fprintf(stderr, "Error in %s\n", argv[0]); it would crash!
    fprintf(stderr, "Error: Program invoked with an empty argument list (argc=0).\n");
    fprintf(stderr, "Unable to determine program name.\n");
    return EXIT_FAILURE;
  }

  char func_name[512];
  
  if(argc == 1) { //default
    argv[0] = "default";
  }

  if(argc >= 2) {
    if(argv[1][0] == '-') { // leading '-' args are checked for -h/--help first
      for(int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
          fprintf(stderr, "Usage: %s <function_name> [args...]\n", argv[0]);
          return 0; /* help request = success (as with host forge -h) */
        }
      }
      argv[0] = "default";
    }
    else {
      argv++;
      argc--;
    }
  }

  snprintf(func_name, sizeof(func_name), "function_%s", argv[0]);

  void *handle = SELF_HANDLE;
  if (!handle) {
    fprintf(stderr, "Failed to get self handle: %s\n", ERROR_MSG());
    return 1;
  }

  int (*func)(int, char **) =
      (int (*)(int, char **))GET_FUNC(handle, func_name);
  if (!func) {
    fprintf(stderr, "Function '%s' not found: %s\n", func_name, ERROR_MSG());
    return 1;
  }
  int ret = func(argc, argv);
  return ret;
}