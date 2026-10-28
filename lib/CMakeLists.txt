set(MONOCYPHER_DIR ${PROJECT_SOURCE_DIR}/third_party/monocypher)

add_library(monocypher STATIC
    ${MONOCYPHER_DIR}/monocypher.c
    ${MONOCYPHER_DIR}/monocypher-ed25519.c
)
target_include_directories(monocypher PUBLIC ${MONOCYPHER_DIR})

# Portable boot core, shared by the bootloader, the application and the tests.
add_library(sota_core STATIC
    src/boot.c
    src/bootstate.c
    src/crc32.c
    src/image.c
    src/update.c
)
target_include_directories(sota_core PUBLIC include)
target_link_libraries(sota_core PUBLIC monocypher)
target_compile_options(sota_core PRIVATE ${SOTA_WARNINGS})
