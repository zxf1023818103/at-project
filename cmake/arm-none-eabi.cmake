# Generic ARM bare-metal cross toolchain (arm-none-eabi-*).
#
# Chip-specific toolchain files (amebad-km0.cmake, ...) include this file
# and then append -march/-mthumb/-mfpu/-mfloat-abi via CMAKE_<LANG>_FLAGS_INIT,
# so a single project tree can target multiple ARM Cortex-M cores by swapping
# CMAKE_TOOLCHAIN_FILE.
#
# Toolchain root resolution order:
#   1. -DAMEBAD_TOOLCHAIN_ROOT=<path> on the cmake command line
#   2. AMEBAD_TOOLCHAIN_ROOT environment variable
#   3. <repo>/toolchain/asdk-10.3.1-4354/linux/newlib/  (default install path
#      of scripts/install_amebad_toolchain.sh — gitignored)
#   4. /opt/rtk-toolchain/asdk-10.3.1-4354/linux/newlib/  (shared install,
#      compatible with the newer ameba-rtos SDK layout)
#   5. SDK vendored copy at
#      platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/
#      project_lp/toolchain/linux/asdk-10.3.1/linux/newlib/
#   6. PATH (assume arm-none-eabi-gcc is already discoverable)

if(NOT DEFINED AMEBAD_TOOLCHAIN_ROOT AND DEFINED ENV{AMEBAD_TOOLCHAIN_ROOT})
    set(AMEBAD_TOOLCHAIN_ROOT "$ENV{AMEBAD_TOOLCHAIN_ROOT}")
endif()

if(NOT DEFINED AMEBAD_TOOLCHAIN_ROOT)
    # REALPATH so a symlinked cmake/ resolves to the real on-disk location
    # before we walk up to <repo>/toolchain/ or platform/.
    get_filename_component(_self_dir "${CMAKE_CURRENT_LIST_FILE}" REALPATH)
    get_filename_component(_self_dir "${_self_dir}" DIRECTORY)

    set(_candidates
        "${_self_dir}/../toolchain/asdk-10.3.1-4354/linux/newlib"
        "/opt/rtk-toolchain/asdk-10.3.1-4354/linux/newlib"
        "${_self_dir}/../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp/toolchain/linux/asdk-10.3.1/linux/newlib"
    )
    foreach(_c IN LISTS _candidates)
        if(EXISTS "${_c}/bin/arm-none-eabi-gcc")
            get_filename_component(AMEBAD_TOOLCHAIN_ROOT "${_c}" ABSOLUTE)
            break()
        endif()
    endforeach()
    unset(_self_dir)
    unset(_candidates)
    unset(_c)
endif()

if(CMAKE_HOST_WIN32)
    set(_exe ".exe")
else()
    set(_exe "")
endif()

if(AMEBAD_TOOLCHAIN_ROOT)
    set(_prefix "${AMEBAD_TOOLCHAIN_ROOT}/bin/arm-none-eabi-")
else()
    # Fall back to PATH lookup.
    set(_prefix "arm-none-eabi-")
endif()

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   "${_prefix}gcc${_exe}"     CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${_prefix}g++${_exe}"     CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER "${_prefix}gcc${_exe}"     CACHE FILEPATH "")
set(CMAKE_AR           "${_prefix}ar${_exe}"      CACHE FILEPATH "")
set(CMAKE_RANLIB       "${_prefix}ranlib${_exe}"  CACHE FILEPATH "")
set(CMAKE_OBJCOPY      "${_prefix}objcopy${_exe}" CACHE FILEPATH "")
set(CMAKE_OBJDUMP      "${_prefix}objdump${_exe}" CACHE FILEPATH "")
set(CMAKE_NM           "${_prefix}nm${_exe}"      CACHE FILEPATH "")
set(CMAKE_STRIP        "${_prefix}strip${_exe}"   CACHE FILEPATH "")
set(CMAKE_SIZE         "${_prefix}size${_exe}"    CACHE FILEPATH "")

# Bare-metal: the compiler-probe link step has no runtime to link against, so
# build a static library instead of an executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(AMEBAD_TOOLCHAIN_ROOT)
    set(CMAKE_FIND_ROOT_PATH "${AMEBAD_TOOLCHAIN_ROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---------------------------------------------------------------------------
# Default compile / link flags mirrored from
# platform/ameba-rtos-d/.../project_{lp,hp}/asdk/Makefile.include.gen
# (GLOBAL_CFLAGS and LD_ARG sections).
#
# Intentionally omitted because they litter the build tree with side files:
#   -MMD -MP        -> *.d   (CMake's own dep tracking already covers this)
#   -save-temps     -> *.i / *.s
#   -fstack-usage   -> *.su
#   -Wl,-Map=...    -> text.map (CMake recipe can re-add per target if wanted)
#
# Optimisation level is left to CMAKE_BUILD_TYPE (default MinSizeRel below).
# Chip-specific -march / -mthumb / -mfpu / -mfloat-abi / -mcmse live in the
# per-chip toolchain files (amebad-km0.cmake, ...).
# ---------------------------------------------------------------------------

set(_arm_common_flags
    -ffunction-sections
    -fdata-sections
    -fno-jump-tables          # SDK note: avoids libgcc _thumb1_case_uqi link issue
    -nostartfiles
    -nostdlib
    -Wall
    -Wextra
    -Wpointer-arith
    -Wundef
    -Wno-write-strings
    -Wno-maybe-uninitialized
)
list(JOIN _arm_common_flags " " _arm_common_flags_str)

# Language standards: gnu17 / gnu++17 expressed via canonical CMake knobs so
# consumers can override per target without fighting a hard-coded -std flag.
# Matches doc/toolchain.md (all Ameba platforms default to C17 / C++14, project
# pins C++ to gnu++17).
set(CMAKE_C_STANDARD            17)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_C_EXTENSIONS          ON)
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        ON)

# Plain `set()` (not string(APPEND)) so re-loads of this toolchain file in the
# same CMake process — which happen during compiler detection and project()
# initialisation — overwrite rather than concatenate. Chip toolchains then
# append -march/-mthumb/... once on top.
set(CMAKE_C_FLAGS_INIT   "${_arm_common_flags_str} -Wstrict-prototypes")
# -frtti + -fno-exceptions matches bare-metal C++ defaults across the project.
# -fno-use-cxa-atexit matches the ameba-arduino-d platform.txt recipe.
set(CMAKE_CXX_FLAGS_INIT "${_arm_common_flags_str} -frtti -fno-exceptions -fno-use-cxa-atexit")
set(CMAKE_ASM_FLAGS_INIT "${_arm_common_flags_str}")

set(_arm_link_flags
    -nostartfiles
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--warn-section-align
    -Wl,--cref
    -Wl,--build-id=none
)
list(JOIN _arm_link_flags " " _arm_link_flags_str)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_arm_link_flags_str}")

unset(_arm_common_flags)
unset(_arm_common_flags_str)
unset(_arm_link_flags)
unset(_arm_link_flags_str)
unset(_exe)
unset(_prefix)

# Default CMAKE_BUILD_TYPE = MinSizeRel (=-Os). Realtek SDK GLOBAL_CFLAGS pins
# -Os on both KM0 LP and KM4 HP — matching that avoids surprise size deltas
# vs. the SDK Makefile build. CACHE without FORCE so an explicit
# -DCMAKE_BUILD_TYPE=Debug still wins. Guarded against multi-config generators.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Default to SDK-matching -Os")
endif()
