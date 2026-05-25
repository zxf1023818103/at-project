include_guard()

# Adds component/lwip_dhcpd (Bouffalo's port of the lwIP DHCP server
# app — sits at component/ top level instead of inside bouffalo_sdk
# because it depends on cmake/lwip.cmake's lwipcore target, which is
# defined by the upstream submodule wrapper). Wired by
# component/CMakeLists.txt's auto-enumeration.

if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "AT_PROJECT_ROOT must be set before including cmake/lwip_dhcpd.cmake")
endif()

add_subdirectory(${AT_PROJECT_ROOT}/component/lwip_dhcpd
                 ${CMAKE_BINARY_DIR}/component/lwip_dhcpd)

# Stitch into the bouffalo_sdk umbrella so downstream just links
# bouffalo_sdk and gets the DHCP server transitively. Ameba projects
# don't define bouffalo_sdk, hence the guard.
if(TARGET bouffalo_sdk)
    target_link_libraries(bouffalo_sdk INTERFACE lwip_dhcpd)
endif()
