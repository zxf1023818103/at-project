# bflb_autoconf — generates <bin>/generated/autoconf.h from every CMake
# CONFIG_* variable (plus BOUFFALO_CHIP / CPU_ID / CPU_MODEL when set) and
# exposes it as a forced -include header to anything that links against it.
#
# Extracted from bouffalo_sdk/cmake/extension.cmake lines 360-425 — the
# variable-namespace walker that the SDK ran from inside its project() macro.
#
# SDK -> native CMake mapping:
#   ${BUILD_DIR}/generated/autoconf.h        -> ${CMAKE_CURRENT_BINARY_DIR}/generated/autoconf.h
#   ${CHIP}                                  -> ${BOUFFALO_CHIP}
#   sdk_add_compile_options(-include …)      -> target_compile_options(<lib> INTERFACE -include …)
#   sdk_add_include_directories(.)           -> target_include_directories(<lib> INTERFACE <gen-dir>)
#
# The linker.ld cpp-expansion that lived just above the macros in the SDK
# was intentionally dropped — each bl<chip>_board_config now expands its own
# linker template via add_custom_command.

set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_autoconf_h     "${_gen_dir}/autoconf.h")
set(_autoconf_h_new "${_gen_dir}/autoconf_new.h")

file(MAKE_DIRECTORY "${_gen_dir}")

# Always rebuild the "new" candidate first; the rename-or-skip MD5 check
# below decides whether to actually update the canonical file.
file(REMOVE "${_autoconf_h_new}")
file(APPEND "${_autoconf_h_new}"
    "#define _ZERO_WITH_COMMA_1 0,\n"
    "#define _second_arg(__ignored, val, ...) val\n"
    "#define _is_enabled(x)                __is_enabled(x)\n"
    "#define __is_enabled(val)             ___is_enabled(_ZERO_WITH_COMMA_##val)\n"
    "#define ___is_enabled(junk_or_comma)  _second_arg(junk_or_comma 1, 0)\n"
    "#define IS_ENABLED(config)            _is_enabled(config)\n"
)

get_cmake_property(_var_names VARIABLES)
foreach(_var ${_var_names})
    if(_var MATCHES "^CONFIG_")
        set(_val ${${_var}})
        if("${_val}" MATCHES "^(0[xX])?[0-9a-fA-F]+$")
            file(APPEND "${_autoconf_h_new}" "#define ${_var} ${_val}\n")
        elseif("${_val}" STREQUAL "y")
            file(APPEND "${_autoconf_h_new}" "#define ${_var} 1\n")
        elseif("${_val}" STREQUAL "n")
            file(APPEND "${_autoconf_h_new}" "#undef ${_var}\n")
        else()
            file(APPEND "${_autoconf_h_new}" "#define ${_var} \"${_val}\"\n")
        endif()
    endif()
endforeach()

if(DEFINED BOUFFALO_CHIP)
    string(TOUPPER "${BOUFFALO_CHIP}" _chip_uc)
    file(APPEND "${_autoconf_h_new}" "#define ${_chip_uc} 1\n")
    # ...and as a string, so code shared across chips (hostname / BLE advert name)
    # can name the chip it was built for instead of hardcoding one of them.
    file(APPEND "${_autoconf_h_new}" "#define BOUFFALO_CHIP_NAME \"${BOUFFALO_CHIP}\"\n")
endif()

if(DEFINED CPU_ID AND NOT "${CPU_ID}" STREQUAL "")
    string(TOUPPER "${CPU_ID}" _cpu_id_uc)
    file(APPEND "${_autoconf_h_new}" "#define CPU_${_cpu_id_uc} 1\n")
endif()

if(DEFINED CPU_MODEL AND NOT "${CPU_MODEL}" STREQUAL "")
    string(TOUPPER "${CPU_MODEL}" _cpu_model_uc)
    file(APPEND "${_autoconf_h_new}" "#define CPU_MODEL_${_cpu_model_uc} 1\n")
endif()

# Only overwrite the canonical autoconf.h if contents actually changed —
# avoids touching the mtime on every cmake reconfigure, which would force
# a full TU rebuild downstream.
if(EXISTS "${_autoconf_h}")
    file(MD5 "${_autoconf_h}"     _hash_old)
    file(MD5 "${_autoconf_h_new}" _hash_new)
    if(NOT ("${_hash_old}" STREQUAL "${_hash_new}"))
        file(REMOVE "${_autoconf_h}")
        file(RENAME "${_autoconf_h_new}" "${_autoconf_h}")
    else()
        file(REMOVE "${_autoconf_h_new}")
    endif()
else()
    file(RENAME "${_autoconf_h_new}" "${_autoconf_h}")
endif()

add_library(bflb_autoconf INTERFACE)
target_include_directories(bflb_autoconf INTERFACE "${_gen_dir}")
target_compile_options(bflb_autoconf INTERFACE
    "SHELL:-include ${_autoconf_h}"
)
