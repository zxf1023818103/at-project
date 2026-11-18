# Explicit opt-in from the standalone BL618DG validation project only.
file(GLOB zstd_common CONFIGURE_DEPENDS zstd/common/*.c)
file(GLOB zstd_compress CONFIGURE_DEPENDS zstd/compress/*.c)
list(FILTER zstd_compress EXCLUDE REGEX "/zstdmt_compress.c$")
add_library(serial_codecs STATIC ${zstd_common} ${zstd_compress} lz4/lz4.c)
target_include_directories(serial_codecs PUBLIC zstd lz4)
target_link_libraries(serial_codecs PRIVATE sdk_intf_lib)
target_compile_definitions(serial_codecs PRIVATE ZSTD_DISABLE_ASM NDEBUG)
target_compile_options(serial_codecs PRIVATE -w)
