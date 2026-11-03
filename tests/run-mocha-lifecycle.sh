#!/bin/sh
set -eu
mkdir -p build-host
# Compile the actual vendored lifecycle, not a second implementation of it.
awk '/^int iosuhaxHandle / || /^int mochaInitDone / || /^uint32_t mochaApiVersion / { print }
     /^MochaUtilsStatus Mocha_InitLibrary\(/ { emit = 1 }
     /^MochaUtilsStatus Mocha_CheckAPIVersion\(/ { emit = 0 }
     emit { print }' source/libmocha/source/utils.cpp > build-host/mocha-lifecycle.inc
"${HOSTCC:-cc}" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DUIF_ASSISTANT_HOST_TEST -Iinclude -Itests -c source/assistant_title_folders.c -o build-host/title-folders-lifecycle.o
"${HOSTCXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Iinclude -Itests -Ibuild-host tests/assistant_mocha_lifecycle_tests.cpp \
    build-host/title-folders-lifecycle.o -o build-host/assistant-mocha-lifecycle-tests
build-host/assistant-mocha-lifecycle-tests
