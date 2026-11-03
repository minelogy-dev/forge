// Source src/parser.c
// Include include
// Include lib/include
#include <build.h>
#include "parser.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
  char conf[] = "other_compiler=MSVC\n"
                "other_compiler_path=/missing/cc\n"
                "note=compiler=CLANG\n"
                "# compiler=MSVC\n"
                "  compiler=GCC # chosen compiler\r\n"
                "\tcompiler_path=gcc\n";
  char *value = get_compiler(conf);
  TEST(value && strcmp(value, "GCC") == 0);
  free(value);
  value = get_compiler_path(conf);
  TEST(value && strcmp(value, "gcc") == 0);
  free(value);

  char unrelated[] = "my_compiler=MSVC\nvalue=compiler=CLANG\n";
  TEST(get_compiler(unrelated) == NULL);
  TEST(get_compiler(NULL) == NULL);

  char overrides[] = "compiler=GCC\ncompiler_linux=CLANG\n"
                     "compiler_windows=MSVC\ncompiler_mac_os=CLANG\n";
  value = get_compiler(overrides);
#if defined(__linux__) || defined(__APPLE__)
  TEST(value && strcmp(value, "CLANG") == 0);
#elif defined(_WIN32)
  TEST(value && strcmp(value, "MSVC") == 0);
#else
  TEST(value && strcmp(value, "GCC") == 0);
#endif
  free(value);
  PASS();
  return 0;
}
