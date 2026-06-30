# component/amebad/string_rom — AmebaD mask-ROM swlib "string" library: the
# str* / mem* / Diag*printf* / rand* / *scanf / char-conversion ROM routines and
# their headers (strproc.h / memproc.h / printf.h / diag.h / rand.h / va_list.h /
# local.h / str-two-way.h). Relocated here from
#   sdk_headers/component/soc/realtek/amebad/swlib/string/
# so the swlib-string headers and their ROM symbol addresses live together.
#
#   amebad_string_rom (INTERFACE)  exposes the headers. amebad_sdk_includes links
#                                  it, so every consumer keeps getting them.
#   string_rom_km{4,0}.ld          the mask-ROM addresses of those routines,
#                                  partitioned out of the per-core
#                                  rlx8721d_rom_symbol_acut.ld. Each KM-image link
#                                  stage cats the one for its core via
#                                  AMEBAD_STRING_ROM_KM{4,0}_LD (KM4 addresses !=
#                                  KM0 addresses, hence two tables). NOTE: cat it
#                                  BEFORE app_km0's romalias fragment, which
#                                  PROVIDE(memcpy=_memcpy) references _memcpy/_memset.

add_library(amebad_string_rom INTERFACE)
target_include_directories(amebad_string_rom SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")

set(AMEBAD_STRING_ROM_KM4_LD "${CMAKE_CURRENT_SOURCE_DIR}/string_rom_km4.ld"
    CACHE INTERNAL "AmebaD KM4 (HP) swlib-string ROM-symbol PROVIDE linker script")
set(AMEBAD_STRING_ROM_KM0_LD "${CMAKE_CURRENT_SOURCE_DIR}/string_rom_km0.ld"
    CACHE INTERNAL "AmebaD KM0 (LP) swlib-string ROM-symbol PROVIDE linker script")
