# bflb_bsp_common — optional BSP peripheral drivers (LCD, audio, touch, eth phy, SDH, etc.)
# (extracted from platform/bouffalo_sdk/bsp/common). All gated by CONFIG_BSP_*.

set(BFLB_BSP_COMMON_SOURCES)
set(BFLB_BSP_COMMON_PUBLIC_INCLUDES)

if(CONFIG_BSP_ETH_PHY)
    list(APPEND BFLB_BSP_COMMON_SOURCES
        eth_phy/eth_phy.c
        eth_phy/eth_phy_port.c
        eth_phy/ephy_general.c
        eth_phy/ephy_lan8720.c)
    list(APPEND BFLB_BSP_COMMON_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/eth_phy)
    if(CONFIG_BSP_LWIP_NETIF_EMAC)
        list(APPEND BFLB_BSP_COMMON_SOURCES eth_phy/lwip_netif_emac/lwip_netif_emac.c)
        list(APPEND BFLB_BSP_COMMON_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/eth_phy/lwip_netif_emac)
    endif()
endif()

if(CONFIG_BSP_SDH_SD)
    list(APPEND BFLB_BSP_COMMON_SOURCES
        sdh/osal/sdh_osal.c
        sdh/host/sdh_host.c
        sdh/sd/sdh_sd.c)
    list(APPEND BFLB_BSP_COMMON_PUBLIC_INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/sdh/osal
        ${CMAKE_CURRENT_LIST_DIR}/sdh/host
        ${CMAKE_CURRENT_LIST_DIR}/sdh/sd)
endif()

if(CONFIG_BSP_LCD)
    list(APPEND BFLB_BSP_COMMON_SOURCES
        lcd/lcd.c
        lcd/font.c
        lcd/mipi_dbi/bl_mipi_dbi.c
        lcd/mipi_dbi/gc9307_dbi.c
        lcd/mipi_dbi/ili9488_dbi.c
        lcd/mipi_dbi/ili9486_dbi.c
        lcd/mipi_dbi/ili9341_dbi.c
        lcd/mipi_dbi/jd9853_dbi.c
        lcd/mipi_dbi/nt35510_dbi.c
        lcd/mipi_dbi/st7789v_dbi.c
        lcd/mipi_dbi/st7796_dbi.c
        lcd/mipi_dbi/st77926_dbi_qspi.c
        lcd/mipi_dbi/spd2010_dbi_qspi.c
        lcd/spi/bl_spi_hard_4.c
        lcd/spi/gc9307_spi.c
        lcd/spi/ili9341_spi.c
        lcd/spi/ili9488_spi.c
        lcd/spi/st7796_spi.c
        lcd/spi/st7789v_spi.c
        lcd/spi/st7735_spi.c
        lcd/mipi_dpi/bl_dpi_init_spi_soft_3.c
        lcd/mipi_dpi/bl_dpi_init_spi_soft_4.c
        lcd/mipi_dpi/bl_mipi_dpi_sim.c
        lcd/mipi_dpi/bl_srgb_sim_v2.c
        lcd/mipi_dpi/bl_mipi_dpi_v2.c
        lcd/mipi_dpi/standard_dpi.c
        lcd/mipi_dpi/ili9488_dpi.c
        lcd/mipi_dpi/gc9503v_dpi.c
        lcd/mipi_dpi/st7701s_dpi.c
        lcd/mipi_dsi/mipi_dsi.c
        lcd/mipi_dsi/ili9881c_dsi.c)
    list(APPEND BFLB_BSP_COMMON_PUBLIC_INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/lcd
        ${CMAKE_CURRENT_LIST_DIR}/lcd/mipi_dsi)
endif()

if(CONFIG_BSP_TOUCH)
    list(APPEND BFLB_BSP_COMMON_SOURCES
        touch/touch.c
        touch/ft6x36_i2c.c
        touch/gt911_i2c.c)
    list(APPEND BFLB_BSP_COMMON_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/touch)
endif()

if(CONFIG_BSP_ADC_KEY)
    list(APPEND BFLB_BSP_COMMON_SOURCES adckey/adckey.c)
    list(APPEND BFLB_BSP_COMMON_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/adckey)
endif()

if(CONFIG_BSP_IMAGE_SENSOR)
    list(APPEND BFLB_BSP_COMMON_SOURCES image_sensor/image_sensor.c)
    list(APPEND BFLB_BSP_COMMON_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/image_sensor)
endif()

if(CONFIG_BSP_AUDIO_CODEC)
    list(APPEND BFLB_BSP_COMMON_SOURCES
        audio_codec/audio_codec.c
        audio_codec/audio_codec_es8311.c
        audio_codec/audio_codec_es8388.c
        audio_codec/audio_codec_es8389.c
        audio_codec/audio_codec_jy6166.c
        audio_codec/audio_codec_jy6311.c
        audio_codec/audio_codec_wm8978.c)
    if(CONFIG_AUDIO_CODEC_JY6316_ENABLE)
        list(APPEND BFLB_BSP_COMMON_SOURCES audio_codec/audio_codec_jy6316.c)
    endif()
    if(CONFIG_BSP_AUDIO_CODEC_BL_ENABLE)
        list(APPEND BFLB_BSP_COMMON_SOURCES audio_codec/audio_codec_bl.c)
    endif()
    list(APPEND BFLB_BSP_COMMON_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/audio_codec)
endif()

if(CONFIG_BSP_AUDIO_PLAYER)
    list(APPEND BFLB_BSP_COMMON_SOURCES
        audio_player/wave_player.c
        audio_player/mp3_player.c)
    list(APPEND BFLB_BSP_COMMON_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/audio_player)
endif()

# Message Router has its own nested CMakeLists — keep its add_subdirectory entry
# only when enabled. It still uses target_sources(app ...); leaving as-is for now.
if(CONFIG_BSP_MR)
    add_subdirectory(msg_router/device)
endif()

# Only create the library if at least one source got selected (otherwise we'd
# emit an empty static lib, which CMake accepts but adds noise).
if(BFLB_BSP_COMMON_SOURCES)
    add_library(bflb_bsp_common STATIC ${BFLB_BSP_COMMON_SOURCES})
    target_include_directories(bflb_bsp_common PUBLIC ${BFLB_BSP_COMMON_PUBLIC_INCLUDES})

    # Compile defs that the original CMakeLists added globally
    if(CONFIG_BSP_SDH_SD)
        string(TOLOWER ${CHIP} _chip_name_str)
        target_compile_definitions(bflb_bsp_common PRIVATE CHIP_GLB_HEADER="${_chip_name_str}_glb.h")
    endif()
    if(CONFIG_BSP_CSI)
        target_compile_definitions(bflb_bsp_common PUBLIC CONFIG_BSP_CSI)
    endif()

    if(TARGET sdk_intf_lib)
        target_link_libraries(bflb_bsp_common PRIVATE sdk_intf_lib)
    endif()
endif()
