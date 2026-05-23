# bflb_bsp — Board Support Package (boards + common BSP drivers)
# (extracted from platform/bouffalo_sdk/bsp).
#
# This top file only dispatches to sub-CMakeLists; the SDK-side forwarder
# still owns SDK_VERSION macro setup + sdk_version.c since those depend on
# PROJECT_SDK_VERSION and target the SDK build's `app` static lib.

if(BOARD_DIR)
    add_subdirectory(${BOARD_DIR}/${BOARD} ${CMAKE_CURRENT_BINARY_DIR}/${BOARD})
else()
    add_subdirectory(board/${BOARD} ${CMAKE_CURRENT_BINARY_DIR}/${BOARD})
endif()

add_subdirectory(common)
