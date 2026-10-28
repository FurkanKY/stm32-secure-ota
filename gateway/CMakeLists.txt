# Stand-alone firmware for a second board: no bootloader, linked at the start
# of flash.
add_executable(sota_gateway main.c)
target_link_libraries(sota_gateway PRIVATE board_stm32f4 sota_core)
target_compile_options(sota_gateway PRIVATE ${SOTA_WARNINGS})
sota_firmware_image(sota_gateway ${SOTA_FLASH_BASE} 0x100000)
