cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(amebadplus_secure C ASM)
    get_filename_component(AT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
endif()
include(ExternalProject)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(NOT TFM_PYTHON)
    foreach(candidate ${AT_ROOT}/build/amebadplus/tfm-python/bin/python3 ${AT_ROOT}/.venv-tfm/bin/python3)
        if(EXISTS ${candidate})
            set(TFM_PYTHON ${candidate})
            break()
        endif()
    endforeach()
endif()
if(NOT TFM_PYTHON)
    set(TFM_PYTHON ${Python3_EXECUTABLE})
endif()
set(TFM_PYTHON ${TFM_PYTHON} CACHE FILEPATH "Python with TF-M tools/requirements.txt installed")
execute_process(COMMAND ${TFM_PYTHON} -c "import jinja2, yaml, jsonschema, cryptography, click"
    RESULT_VARIABLE _tfm_python_check ERROR_QUIET)
if(NOT _tfm_python_check EQUAL 0)
    message(FATAL_ERROR "TF-M Python dependencies missing; run make -C project/amebadplus tfm_deps or set TFM_PYTHON")
endif()
set(_tfm_src ${AT_ROOT}/component/trusted-firmware-m_config/trusted-firmware-m)
set(_tfm_bin ${CMAKE_CURRENT_BINARY_DIR}/tfm_spe)
set(_tfm_export ${CMAKE_CURRENT_BINARY_DIR}/tfm_spe_export)
get_filename_component(_tc_bin ${CMAKE_C_COMPILER} DIRECTORY)
set(_env ${CMAKE_COMMAND} -E env "PATH=${_tc_bin}:$ENV{PATH}")
set(_mbedcrypto ${CMAKE_CURRENT_BINARY_DIR}/mbedcrypto-src)
execute_process(COMMAND ${TFM_PYTHON}
    ${AT_ROOT}/component/amebadplus/image_tool/prepare_tfm.py ${AT_ROOT} ${_mbedcrypto}
    COMMAND_ERROR_IS_FATAL ANY)
set(_cached_deps)
foreach(dep CMSIS QCBOR)
    string(TOLOWER ${dep} lower)
    set(candidate ${AT_ROOT}/build/app_km4/app_km4_secure/tfm_spe/lib/ext/${lower}-src)
    if(EXISTS ${candidate}/CMakeLists.txt OR EXISTS ${candidate}/CMSIS/Core/Include/core_cm33.h)
        list(APPEND _cached_deps -D${dep}_PATH=${candidate})
    endif()
endforeach()
ExternalProject_Add(tfm_spe
    SOURCE_DIR ${_tfm_src} BINARY_DIR ${_tfm_bin}
    CONFIGURE_COMMAND ${_env} ${CMAKE_COMMAND} -G Ninja -S ${_tfm_src} -B ${_tfm_bin}
        -DTFM_PLATFORM=${CMAKE_CURRENT_LIST_DIR}/platform/amebadplus
        -DTFM_TOOLCHAIN_FILE=${_tfm_src}/toolchain_GNUARM.cmake
        -DCROSS_COMPILE=arm-none-eabi -DCMAKE_PROGRAM_PATH=${_tc_bin}
        -DPython3_EXECUTABLE=${TFM_PYTHON}
        -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=${_tfm_export}
        ${_cached_deps}
        -DBL2=OFF -DDPLUS_TFM_STORAGE=${AT_ENABLE_STORAGE} -DDPLUS_EFUSE_DEBUG=${AT_EFUSE_DEBUG}
        -DMBEDCRYPTO_PATH=${_mbedcrypto} -DMBEDCRYPTO_FORCE_PATCH=OFF
        -DMBEDCRYPTO_GIT_REMOTE=${AT_ROOT}/component/mbedtls_config/mbedtls
        -DMBEDCRYPTO_VERSION=mbedtls-3.6.4
    BUILD_COMMAND ${_env} ${CMAKE_COMMAND} --build ${_tfm_bin} -j8
    INSTALL_COMMAND ${_env} ${CMAKE_COMMAND} --install ${_tfm_bin}
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS ${_tfm_bin}/bin/tfm_s.bin ${_tfm_export}/interface/lib/s_veneers.o)
add_custom_target(app_km4_secure DEPENDS tfm_spe)
