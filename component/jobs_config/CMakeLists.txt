# jobs_config — AWS IoT Jobs (submodule component/jobs_config/jobs @ v2.0.1): assembles/parses
# the AWS IoT Jobs MQTT topics and the job documents, plus the otaJobParser helper that pulls the
# OTA fields out of an AFR-OTA job document (job_parser.c + ota_job_handler.c).
#
# HAND WRAPPER, NOT the upstream add_subdirectory: jobs' own CMakeLists uses FetchContent to
# clone coreJSON from GitHub at configure time (offline/controlled builds can't do that). Instead
# we include jobsFilePaths.cmake to reuse the upstream file list and link the project's own
# corejson_config. The `jobs_config` INTERFACE is the umbrella consumers link.
#
# Depends on coreJSON: jobs.c + the otaJobParser parse documents with JSON_Search etc. No config
# header (jobs has no *_DO_NOT_USE_CUSTOM_CONFIG gate). Cross-platform: no chip gate.
include("${CMAKE_CURRENT_SOURCE_DIR}/jobs/jobsFilePaths.cmake")  # JOBS_SOURCES, JOBS_INCLUDE_PUBLIC_DIRS, OTA_HANDLER_SOURCES, OTA_HANDLER_INCLUDES

add_library(aws_iot_jobs STATIC EXCLUDE_FROM_ALL
    ${JOBS_SOURCES}
    ${OTA_HANDLER_SOURCES}
)
# PUBLIC (SYSTEM, to keep the vendor headers out of -Werror): jobs' own include/ + the
# otaJobParser include/ (from jobsFilePaths.cmake).
target_include_directories(aws_iot_jobs SYSTEM PUBLIC
    ${JOBS_INCLUDE_PUBLIC_DIRS}
    ${OTA_HANDLER_INCLUDES}
)
# coreJSON backend (headers + symbols), the project's own — NOT the FetchContent'd one.
target_link_libraries(aws_iot_jobs PUBLIC corejson_config)
target_compile_options(aws_iot_jobs PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `jobs_config` to get AWS IoT Jobs.
add_library(jobs_config INTERFACE)
target_link_libraries(jobs_config INTERFACE aws_iot_jobs)
