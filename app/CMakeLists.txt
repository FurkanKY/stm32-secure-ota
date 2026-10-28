# sota_add_app(<name> SLOT A|B VERSION x.y.z COUNTER n [NO_CONFIRM])
# Builds the demo app linked for one slot and signs it: <name>.signed.bin
function(sota_add_app name)
    cmake_parse_arguments(APP "NO_CONFIRM" "SLOT;VERSION;COUNTER" "" ${ARGN})

    if(APP_SLOT STREQUAL "A")
        set(index 0)
        set(base ${SOTA_SLOT_A_BASE})
        set(size ${SOTA_SLOT_A_SIZE})
    elseif(APP_SLOT STREQUAL "B")
        set(index 1)
        set(base ${SOTA_SLOT_B_BASE})
        set(size ${SOTA_SLOT_B_SIZE})
    else()
        message(FATAL_ERROR "sota_add_app(${name}): SLOT must be A or B")
    endif()
    math(EXPR origin "${base} + ${SOTA_HEADER_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR length "${size} - ${SOTA_HEADER_SIZE}" OUTPUT_FORMAT HEXADECIMAL)

    add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/main.c)
    target_compile_definitions(${name} PRIVATE APP_SLOT=${index})
    if(APP_NO_CONFIRM)
        target_compile_definitions(${name} PRIVATE APP_NO_CONFIRM=1)
    endif()
    target_link_libraries(${name} PRIVATE board_stm32f4 sota_core)
    target_compile_options(${name} PRIVATE ${SOTA_WARNINGS})
    sota_firmware_image(${name} ${origin} ${length})

    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${SOTA_OTATOOL} sign ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
                -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.signed.bin
                --key ${SOTA_SIGNING_KEY} --layout ${SOTA_LAYOUT_JSON}
                --slot ${APP_SLOT} --version ${APP_VERSION}
                --security-counter ${APP_COUNTER}
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/${name}.signed.bin
        VERBATIM)
endfunction()

# Factory image and a normal update.
sota_add_app(app_v1_a SLOT A VERSION 1.0.0 COUNTER 1)
sota_add_app(app_v2_b SLOT B VERSION 2.0.0 COUNTER 1)
# A release that never confirms itself: must be rolled back.
sota_add_app(app_bad_b SLOT B VERSION 2.1.0 COUNTER 1 NO_CONFIRM)
# A security fix that raises the counter, and an old build it locks out.
sota_add_app(app_v3_b SLOT B VERSION 3.0.0 COUNTER 2)
sota_add_app(app_v3_a SLOT A VERSION 3.0.1 COUNTER 2)
sota_add_app(app_old_a SLOT A VERSION 0.9.0 COUNTER 1)
