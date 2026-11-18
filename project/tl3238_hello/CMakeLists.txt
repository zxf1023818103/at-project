cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../../cmake/tl3238-toolchain.cmake"
    CACHE FILEPATH "Cross compiler")
project(tl3238_hello LANGUAGES C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(TL3238_ENABLE_BLE OFF)
set(TL3238_ENABLE_CS OFF)
set(TL3238_BARE_METAL ON)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../component/telink_sdk" telink_sdk)
add_executable(tl3238_hello main.c)
set_target_properties(tl3238_hello PROPERTIES SUFFIX ".elf")
target_link_libraries(tl3238_hello PRIVATE telink_sdk)
target_link_options(tl3238_hello PRIVATE "-T${TL3238_LINKER_SCRIPT}"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/tl3238_hello.map")
set_property(TARGET tl3238_hello APPEND PROPERTY LINK_DEPENDS "${TL3238_LINKER_SCRIPT}")
add_custom_command(TARGET tl3238_hello POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:tl3238_hello>"
        "${CMAKE_CURRENT_BINARY_DIR}/tl3238_hello.bin"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:tl3238_hello>" VERBATIM)
