# Generic RISC-V bare-metal cross toolchain (riscv64-unknown-elf-*).
#
# Chip-specific toolchain files (bl602-toolchain.cmake, bl616-toolchain.cmake,
# bl616cl-toolchain.cmake) include this file and then append -march/-mabi/-mtune
# via CMAKE_<LANG>_FLAGS_INIT, so a single project tree can target multiple
# Bouffalo SoCs by swapping CMAKE_TOOLCHAIN_FILE.
#
# Toolchain root resolution order:
#   1. -DBOUFFALO_TOOLCHAIN_ROOT=<path> on the cmake command line
#   2. BOUFFALO_TOOLCHAIN_ROOT environment variable
#   3. the in-repo platform/toolchain_gcc_t-head_<host>/ submodule
#   4. PATH (assume riscv64-unknown-elf-gcc is already discoverable)

if(NOT DEFINED BOUFFALO_TOOLCHAIN_ROOT AND DEFINED ENV{BOUFFALO_TOOLCHAIN_ROOT})
    set(BOUFFALO_TOOLCHAIN_ROOT "$ENV{BOUFFALO_TOOLCHAIN_ROOT}")
endif()

if(NOT DEFINED BOUFFALO_TOOLCHAIN_ROOT)
    # REALPATH so a symlinked cmake/ (e.g. test/bouffalo_toolchain/cmake -> ../../cmake)
    # resolves to the real on-disk location before we walk up to platform/.
    get_filename_component(_self_dir "${CMAKE_CURRENT_LIST_FILE}" REALPATH)
    get_filename_component(_self_dir "${_self_dir}" DIRECTORY)
    if(CMAKE_HOST_WIN32)
        set(_host_tag "windows")
    elseif(CMAKE_HOST_APPLE)
        set(_host_tag "darwin")
    else()
        set(_host_tag "linux")
    endif()
    set(_candidate "${_self_dir}/../platform/toolchain_gcc_t-head_${_host_tag}")
    if(EXISTS "${_candidate}")
        get_filename_component(BOUFFALO_TOOLCHAIN_ROOT "${_candidate}" ABSOLUTE)
    endif()
    unset(_self_dir)
    unset(_host_tag)
    unset(_candidate)
endif()

if(CMAKE_HOST_WIN32)
    set(_exe ".exe")
else()
    set(_exe "")
endif()

if(BOUFFALO_TOOLCHAIN_ROOT)
    set(_prefix "${BOUFFALO_TOOLCHAIN_ROOT}/bin/riscv64-unknown-elf-")
else()
    # Fall back to PATH lookup.
    set(_prefix "riscv64-unknown-elf-")
endif()

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

set(CMAKE_C_COMPILER   "${_prefix}gcc${_exe}"     CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${_prefix}g++${_exe}"     CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER "${_prefix}gcc${_exe}"     CACHE FILEPATH "")
set(CMAKE_AR           "${_prefix}ar${_exe}"      CACHE FILEPATH "")
set(CMAKE_RANLIB       "${_prefix}ranlib${_exe}"  CACHE FILEPATH "")
set(CMAKE_OBJCOPY      "${_prefix}objcopy${_exe}" CACHE FILEPATH "")
set(CMAKE_OBJDUMP      "${_prefix}objdump${_exe}" CACHE FILEPATH "")
set(CMAKE_SIZE         "${_prefix}size${_exe}"    CACHE FILEPATH "")

# `ar crD` matches what the Bouffalo SDK uses for deterministic archives.
set(CMAKE_C_CREATE_STATIC_LIBRARY   "<CMAKE_AR> crD <TARGET> <OBJECTS>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> crD <TARGET> <OBJECTS>")
set(CMAKE_ASM_CREATE_STATIC_LIBRARY "<CMAKE_AR> crD <TARGET> <OBJECTS>")

# Bare-metal: the compiler-probe link step has no runtime to link against, so
# build a static library instead of an executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(BOUFFALO_TOOLCHAIN_ROOT)
    set(CMAKE_FIND_ROOT_PATH "${BOUFFALO_TOOLCHAIN_ROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---------------------------------------------------------------------------
# Default compile / link flags mirrored from
# platform/bouffalo_sdk/cmake/compiler_flags.cmake.
#
# Intentionally omitted because they litter the build tree with side files:
#   -MMD            -> *.d  (CMake's own dependency tracking already covers this)
#   -fstack-usage   -> *.su
#   -save-temps=obj -> *.i / *.ii / *.s
#   -Wl,-Map=...    -> *.map (none in the source today; keep it out if added)
#
# Optimisation level and -g are left to CMAKE_BUILD_TYPE.
# Chip-specific -march / -mabi / -mtune live in the per-chip toolchain files.
# ---------------------------------------------------------------------------

set(_bouffalo_common_flags
    -fno-jump-tables
    -fno-common
    -fms-extensions
    -ffunction-sections
    -fdata-sections
    -fmessage-length=0
    -fstrict-volatile-bitfields
    -Wall
    -Wchar-subscripts
    -Wpointer-arith
    -Wno-format
    -Wundef
    -Wuninitialized
    -Winit-self
    -Wignored-qualifiers
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -fshort-enums
)
list(JOIN _bouffalo_common_flags " " _bouffalo_common_flags_str)

# Language standards: gnu17 / gnu++17 expressed via the canonical CMake knobs
# so consumers can override them per target (set_target_properties) without
# fighting a hard-coded -std flag. EXTENSIONS=ON keeps the GNU dialect.
set(CMAKE_C_STANDARD            17)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_C_EXTENSIONS          ON)
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        ON)

# Plain `set()` (not string(APPEND)) so re-loads of this toolchain file in the
# same CMake process — which happen during compiler detection and project()
# initialisation — overwrite rather than concatenate. Chip toolchains then
# append -march/-mabi/-mtune once on top.
set(CMAKE_C_FLAGS_INIT   "${_bouffalo_common_flags_str}")
# -frtti + -fno-exceptions: C++ runtime knobs the SDK uses. Note: do NOT
# pass -nostdlib here — these flags propagate to the link step too (the
# final ELF is linked through g++ when there are any .cpp TUs), and
# -nostdlib at link suppresses both libgcc (for __udivdi3 / __floatsidf
# / __adddf3) and libc/newlib (for malloc/free that bflb_libc's
# port_memory uses).
set(CMAKE_CXX_FLAGS_INIT "${_bouffalo_common_flags_str} -frtti -fno-exceptions")
set(CMAKE_ASM_FLAGS_INIT "${_bouffalo_common_flags_str}")

set(_bouffalo_link_flags
    -Wl,--cref
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -nostartfiles
    -fms-extensions
    -flto
    -ffunction-sections
    -fdata-sections
    --specs=nano.specs
)
list(JOIN _bouffalo_link_flags " " _bouffalo_link_flags_str)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_bouffalo_link_flags_str}")

unset(_bouffalo_common_flags)
unset(_bouffalo_common_flags_str)
unset(_bouffalo_link_flags)
unset(_bouffalo_link_flags_str)

unset(_exe)
unset(_prefix)
