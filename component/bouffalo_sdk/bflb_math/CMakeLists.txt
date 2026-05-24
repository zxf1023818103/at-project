# bflb_math — small math/encoding helpers (base64, CRC, hex).
# Extracted from bouffalo_sdk/components/utils/math.
# Self-contained (only stdint/string).

add_library(bflb_math STATIC
    src/utils_base64.c
    src/utils_crc.c
    src/utils_hex.c
)

target_include_directories(bflb_math PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
