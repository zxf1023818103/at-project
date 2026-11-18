# Bouffalo replaces project() with a firmware-generating macro. Mbed TLS must
# call the saved CMake builtin (_project) instead. Keep all upstream files
# unchanged: the build-only view symlinks the source and replaces this one call.
set(upstream ${AT_ROOT}/component/mbedtls_config/mbedtls)
set(MBEDTLS_UPSTREAM_DIR ${CMAKE_CURRENT_BINARY_DIR}/mbedtls-source)
file(MAKE_DIRECTORY ${MBEDTLS_UPSTREAM_DIR})
file(GLOB children ${upstream}/*)
foreach(child ${children})
    get_filename_component(name ${child} NAME)
    if(NOT name STREQUAL "CMakeLists.txt" AND NOT EXISTS ${MBEDTLS_UPSTREAM_DIR}/${name})
        file(CREATE_LINK ${child} ${MBEDTLS_UPSTREAM_DIR}/${name} SYMBOLIC)
    endif()
endforeach()
file(READ ${upstream}/CMakeLists.txt top)
string(REPLACE "project(\"Mbed TLS\"" "_project(\"Mbed TLS\"" top "${top}")
file(CONFIGURE OUTPUT ${MBEDTLS_UPSTREAM_DIR}/CMakeLists.txt CONTENT "${top}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${upstream}/CMakeLists.txt)
