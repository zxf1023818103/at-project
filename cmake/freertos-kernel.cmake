include_guard()

# FreeRTOS-Kernel V10.6.2 lives in thirdparty/freertos-kernel/ as a git submodule.
# It is consumed by platform/bouffalo_sdk/components/os/freertos/CMakeLists.txt,
# which references the upstream .c sources and include/ path directly via an
# absolute path relative to BL_SDK_BASE.
#
# at-project's host build does not need to add_subdirectory the kernel here —
# the SDK build picks it up. This glue exists only to suppress the
# "no cmake glue, skipping" warning emitted by thirdparty/CMakeLists.txt.
