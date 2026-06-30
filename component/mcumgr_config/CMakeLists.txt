# mcumgr_config - apache/mynewt-mcumgr 0.7.0 (submodule component/mcumgr_config/mcumgr) as the
# SMP management protocol for AmebaD BLE DFU (make MCUMGR=ON). We compile only the protocol core
# (mgmt + smp) and the img + os command groups; the BLE smp_bt transport + the img/os platform
# impls live in component/amebad/mcumgr_port + app_km4 (mcumgr's transport/ is docs-only and the
# *_impl_* hooks are platform-specific).
#
# mcumgr 0.7.0 targets the old apache-mynewt-core tinycbor (reader/writer CBOR API). This project
# ships intel/tinycbor v7 (flat API). Rather than vendor a second, conflicting CBOR lib, we keep
# ONE tinycbor (v7) and bridge with a small shim: include/mcumgr_compat.{h} (force-included into
# every mcumgr TU) reimplements the reader/writer surface over v7, and five colliding call sites
# (cbor_parser_init / cbor_encoder_init / cbor_encode_bytes_written) are patched on our fork to the
# mcumgr_cbor_* wrappers. See include/mcumgr_compat.h.

set(_m "${CMAKE_CURRENT_SOURCE_DIR}/mcumgr")

# mcumgr's sources include "tinycbor/cbor.h", but this project's tinycbor exposes headers as
# "cbor.h" (its src/ is on the include path). Bridge the prefix with a symlink inc/tinycbor ->
# tinycbor/src so the submodule includes stay unpatched. (cbor_buf_reader.h is NOT in tinycbor src;
# it resolves to our shim in include/tinycbor/ instead.)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/inc")
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/inc/tinycbor")
    file(CREATE_LINK "${AT_ROOT}/component/tinycbor_config/tinycbor/src"
                     "${CMAKE_CURRENT_BINARY_DIR}/inc/tinycbor" SYMBOLIC)
endif()

add_library(mcumgr STATIC EXCLUDE_FROM_ALL
    "${_m}/mgmt/src/mgmt.c"
    "${_m}/smp/src/smp.c"
    "${_m}/cmd/img_mgmt/src/img_mgmt.c"
    "${_m}/cmd/img_mgmt/src/img_mgmt_state.c"
    "${_m}/cmd/img_mgmt/src/img_mgmt_util.c"
    "${_m}/cmd/os_mgmt/src/os_mgmt.c"
    "${_m}/cmd/os_mgmt/src/stubs.c"
    "${_m}/cborattr/src/cborattr.c"
    "${_m}/util/src/mcumgr_util.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcumgr_compat.c"
)

target_include_directories(mcumgr PUBLIC
    "${_m}/mgmt/include"
    "${_m}/smp/include"
    "${_m}/cmd/img_mgmt/include"
    "${_m}/cmd/os_mgmt/include"
    "${_m}/cborattr/include"
    "${_m}/util/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"          # mcumgr_compat.h, syscfg shim, cbor_buf_reader.h
    "${CMAKE_CURRENT_BINARY_DIR}/inc"              # tinycbor/ prefix symlink
)

# -w: silence vendor warnings. -include mcumgr_compat.h: make the v7 CBOR shim + struct defs visible
# in every mcumgr TU before its own includes.
target_compile_options(mcumgr PRIVATE -w -include mcumgr_compat.h)
target_compile_definitions(mcumgr PRIVATE CBORATTR_MAX_SIZE=512)   # cborattr attr-name scratch buffer

# tinycbor is needed at compile time (cbor.h) and at link time (cbor functions). We take only its
# INCLUDE dirs here, NOT a link dependency: in the AmebaD image both `mcumgr` and `tinycbor` ride the
# same cross_refs LINK_GROUP, and a link dep on the tinycbor_config umbrella (whose archive is in the
# group) would form a target dependency cycle. The group resolves the tinycbor symbols at link.
target_include_directories(mcumgr PUBLIC
    $<TARGET_PROPERTY:tinycbor_config,INTERFACE_INCLUDE_DIRECTORIES>)

add_library(mcumgr_config INTERFACE)
target_link_libraries(mcumgr_config INTERFACE mcumgr)
