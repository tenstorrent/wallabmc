# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(atlantis-bmc)

zephyr_linker_sources(SECTIONS sections-rom.ld)

target_sources(app PRIVATE
	src/config.c
	src/dhcp.c
	src/main.c
	src/net.c
	src/power.c
)

target_sources_ifdef(CONFIG_JTAG app PRIVATE
	src/jtag.c
)

target_sources_ifdef(CONFIG_PERSISTENT_STORAGE app PRIVATE
	src/fs.c
)

target_sources_ifdef(CONFIG_REDFISH app PRIVATE
	src/redfish.c
)

if(CONFIG_REDFISH_CRYPTO)
  set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

  foreach(inc_file
    server_cert.der
    server_privkey.der
  )
    generate_inc_file_for_target(app
      src/certs/${inc_file}
      ${gen_dir}/${inc_file}.inc
    )
  endforeach()
endif()

if(CONFIG_PERSISTENT_STORAGE)
  if(CONFIG_CODE_DATA_RELOCATION_SRAM AND CONFIG_SOC_FLASH_STM32)
    include(${ZEPHYR_BASE}/cmake/modules/extensions.cmake)
    FILE(GLOB flash_driver ${ZEPHYR_BASE}/drivers/flash/flash_stm32*.c)
    zephyr_code_relocate(FILES ${flash_driver} LOCATION RAM)
  else()
    message(FATAL_ERROR "flash storage handling is not defined for this platform")
  endif()
endif()
