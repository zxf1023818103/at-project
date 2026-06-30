# fleetprovisioning_config — AWS IoT Fleet Provisioning (submodule
# component/fleetprovisioning_config/fleetprovisioning @ v1.2.2): assembles/parses the Fleet
# Provisioning MQTT topic strings (CreateCertificateFromCsr / CreateKeysAndCertificate /
# RegisterThing + accepted/rejected). The CBOR/JSON payloads are handled by the consumer — this
# library is topic-string only.
#
# Built via the submodule's OWN upstream CMake (add_subdirectory -> the `aws_fleet_provisioning`
# target, from fleetprovisioningFilePaths.cmake), like coremqtt_config. Our
# fleet_provisioning_config.h (this dir) is put on its include path; `fleetprovisioning_config`
# is the INTERFACE umbrella consumers link. No external library dependency. No chip gate.
add_subdirectory(fleetprovisioning "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# fleet_provisioning.c pulls fleet_provisioning_config_defaults.h -> fleet_provisioning_config.h:
# put our config header on the target's path (PUBLIC). SYSTEM keeps it out of -Werror; -w silences.
target_include_directories(aws_fleet_provisioning SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(aws_fleet_provisioning PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `fleetprovisioning_config` to get Fleet Provisioning.
add_library(fleetprovisioning_config INTERFACE)
target_link_libraries(fleetprovisioning_config INTERFACE aws_fleet_provisioning)
