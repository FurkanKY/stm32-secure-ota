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

if(CMAKE_CROSSCOMPILING AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "" FORCE)
endif()

add_subdirectory(lib)

if(SOTA_HOST_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(CMAKE_CROSSCOMPILING)
    set(SOTA_BOARD stm32f407vg CACHE STRING "Directory under boards/")
    set(SOTA_SIGNING_KEY ${PROJECT_SOURCE_DIR}/keys/dev_ed25519.pem
        CACHE FILEPATH "Ed25519 key that signs the demo images")
    set(SOTA_LAYOUT_JSON ${PROJECT_SOURCE_DIR}/boards/${SOTA_BOARD}/layout.json)
    set(SOTA_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)

    include(cmake/layout.cmake)
    sota_read_layout(${SOTA_LAYOUT_JSON})
    configure_file(cmake/board_layout.h.in ${SOTA_GENERATED_DIR}/sota_board_layout.h @ONLY)

    # otatool signs the images; prefer the repository's virtualenv if present.
    foreach(candidate .venv/Scripts/python.exe .venv/bin/python)
        if(NOT Python3_EXECUTABLE AND EXISTS ${PROJECT_SOURCE_DIR}/${candidate})
            set(Python3_EXECUTABLE ${PROJECT_SOURCE_DIR}/${candidate})
        endif()
    endforeach()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(SOTA_OTATOOL ${CMAKE_COMMAND} -E env PYTHONPATH=${PROJECT_SOURCE_DIR}/tools
        ${Python3_EXECUTABLE} -m otatool)

    add_subdirectory(port/stm32f4)
    add_subdirectory(boot)
    add_subdirectory(app)

    # Blank 1 MiB flash image for the simulator (real flash erases to 0xFF).
    set(erased ${CMAKE_BINARY_DIR}/flash_erased.bin)
    add_custom_command(OUTPUT ${erased}
        COMMAND ${Python3_EXECUTABLE} -c
                "import sys; open(sys.argv[1], 'wb').write(bytes([255]) * 0x100000)" ${erased}
        VERBATIM)
    add_custom_target(flash_erased ALL DEPENDS ${erased})
endif()
