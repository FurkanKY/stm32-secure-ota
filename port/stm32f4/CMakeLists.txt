add_library(board_stm32f4 STATIC
    startup.c
    board.c
    flash_f4.c
    can_f4.c
    ../common/console_fmt.c
)
target_include_directories(board_stm32f4 PUBLIC
    ../include
    .
    ${SOTA_GENERATED_DIR}
)
target_link_libraries(board_stm32f4 PUBLIC sota_core)
# startup.c uses a GNU range designator for the IRQ table, so no -Wpedantic.
target_compile_options(board_stm32f4 PRIVATE -Wall -Wextra -Werror)

set(SOTA_PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "")

# Link <target> for a flash window, then produce <target>.bin next to the ELF.
function(sota_firmware_image target origin length)
    set(LD_IMAGE_NAME ${target})
    set(LD_FLASH_ORIGIN ${origin})
    set(LD_FLASH_LENGTH ${length})
    math(EXPR LD_RAM_LENGTH "${SOTA_RAM_SIZE} - 32" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR LD_SHARED_ORIGIN "${SOTA_RAM_BASE} + ${SOTA_RAM_SIZE} - 32" OUTPUT_FORMAT HEXADECIMAL)
    set(script ${CMAKE_CURRENT_BINARY_DIR}/${target}.ld)
    configure_file(${SOTA_PORT_DIR}/image.ld.in ${script} @ONLY)

    set_target_properties(${target} PROPERTIES SUFFIX .elf LINK_DEPENDS ${script})
    target_link_options(${target} PRIVATE
        -T${script}
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map
        -Wl,--print-memory-usage)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}>
                ${CMAKE_CURRENT_BINARY_DIR}/${target}.bin
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/${target}.bin
        VERBATIM)
endfunction()
