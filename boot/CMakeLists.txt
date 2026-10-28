set(keys_c ${CMAKE_CURRENT_BINARY_DIR}/boot_keys.c)
add_custom_command(
    OUTPUT ${keys_c}
    COMMAND ${SOTA_OTATOOL} pubkey ${SOTA_SIGNING_KEY} --c ${keys_c}
    DEPENDS ${SOTA_SIGNING_KEY}
    COMMENT "Embedding public key from ${SOTA_SIGNING_KEY}"
    VERBATIM)

add_executable(sota_boot main.c ${keys_c})
target_include_directories(sota_boot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sota_boot PRIVATE board_stm32f4 sota_core)
target_compile_options(sota_boot PRIVATE ${SOTA_WARNINGS})
sota_firmware_image(sota_boot ${SOTA_BOOT_BASE} ${SOTA_BOOT_SIZE})
