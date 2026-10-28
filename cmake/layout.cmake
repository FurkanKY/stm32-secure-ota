# Read boards/<board>/layout.json into SOTA_* variables, the same file otatool
# reads, so firmware and host tools always agree on the memory map.

function(_sota_hex out value)
    math(EXPR v "${value}" OUTPUT_FORMAT HEXADECIMAL)
    set(${out} ${v} PARENT_SCOPE)
endfunction()

function(sota_read_layout json_path)
    file(READ ${json_path} json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${json_path})

    string(JSON flash_base GET ${json} flash_base)
    string(JSON header_size GET ${json} header_size)
    string(JSON ram_base GET ${json} ram base)
    string(JSON ram_size GET ${json} ram size)
    string(JSON boot_off GET ${json} regions bootloader offset)
    string(JSON boot_size GET ${json} regions bootloader size)
    string(JSON state_off GET ${json} regions bootstate offset)
    string(JSON state_size GET ${json} regions bootstate size)
    string(JSON a_off GET ${json} slots A offset)
    string(JSON a_size GET ${json} slots A size)
    string(JSON b_off GET ${json} slots B offset)
    string(JSON b_size GET ${json} slots B size)

    _sota_hex(SOTA_FLASH_BASE "${flash_base}")
    _sota_hex(SOTA_HEADER_SIZE "${header_size}")
    _sota_hex(SOTA_RAM_BASE "${ram_base}")
    _sota_hex(SOTA_RAM_SIZE "${ram_size}")
    _sota_hex(SOTA_BOOT_BASE "${flash_base} + ${boot_off}")
    _sota_hex(SOTA_BOOT_SIZE "${boot_size}")
    # The boot state region is split into the two journal pages.
    _sota_hex(SOTA_STATE_PAGE_SIZE "${state_size} / 2")
    _sota_hex(SOTA_STATE0_BASE "${flash_base} + ${state_off}")
    _sota_hex(SOTA_STATE1_BASE "${flash_base} + ${state_off} + ${state_size} / 2")
    _sota_hex(SOTA_SLOT_A_BASE "${flash_base} + ${a_off}")
    _sota_hex(SOTA_SLOT_A_SIZE "${a_size}")
    _sota_hex(SOTA_SLOT_B_BASE "${flash_base} + ${b_off}")
    _sota_hex(SOTA_SLOT_B_SIZE "${b_size}")

    foreach(var FLASH_BASE HEADER_SIZE RAM_BASE RAM_SIZE BOOT_BASE BOOT_SIZE
                STATE_PAGE_SIZE STATE0_BASE STATE1_BASE
                SLOT_A_BASE SLOT_A_SIZE SLOT_B_BASE SLOT_B_SIZE)
        set(SOTA_${var} ${SOTA_${var}} PARENT_SCOPE)
    endforeach()
endfunction()
