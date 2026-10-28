cmake_minimum_required(VERSION 3.20)
project(stm32_secure_ota C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SOTA_HOST_TESTS "Build the host unit tests" OFF)
option(SOTA_SANITIZE "Build host tests with ASan and UBSan" OFF)

# Warnings for our own code; third-party sources keep their defaults.
set(SOTA_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Werror)

if(SOTA_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(lib)

if(SOTA_HOST_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
