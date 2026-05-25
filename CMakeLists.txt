cmake_minimum_required(VERSION 3.20)
project(at_project C CXX ASM)

# Root entry for the at_project unified build (currently used by the
# AmebaD SDK's library/Makefile cmake invocation). Bouffalo and the new
# ameba-rtos SDK don't drive a unified CMake build of this tree; they
# add_subdirectory(component) or add_subdirectory(component/thridparty)
# from inside their own project setups instead.

add_subdirectory(component)
add_subdirectory(src)

# Forward src/'s AT_USER_SOURCES / AT_USER_SRC_DIR one more scope up so
# they reach the per-chip project (project/bouffalo, etc.) which adds
# the at-project root as a single subdirectory. At top-level invocations
# (the AmebaD library/Makefile flow) PARENT_SCOPE is silently a no-op.
set(AT_USER_SOURCES ${AT_USER_SOURCES} PARENT_SCOPE)
set(AT_USER_SRC_DIR ${AT_USER_SRC_DIR} PARENT_SCOPE)
