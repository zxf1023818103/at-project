# wifi4_firmware — INTERFACE wrapper around the prebuilt BL602 Wi-Fi 4
# firmware archive. Extracted from
# bouffalo_sdk/components/wireless/wifi4/firmware/.
#
# Carries far more than the .a: the upstream SDK CMakeLists declares a wall
# of CFG_*/RW_*/NX_*/CFG_PHY_* compile defs in this same dir (no source
# files attached), and those defs are required to compile manager/ and
# wpa_supplicant/ — every .c that talks to libfirmware.a includes a header
# that #ifdefs on them. So PROPAGATE them INTERFACE-wide.
#
# Knobs (option vars, default to the SDK's Kconfig defaults so AT firmware
# builds match what BL602 examples produce):
#   CONFIG_STA_MAX               5   max simultaneous STAs on the soft-AP
#   CONFIG_VIRT_DEV_MAX          2   virtual interface count
#   CONFIG_BFLB_IOT_FW_AMPDU     1   TX A-MPDU aggregation
#   CONFIG_BFLB_IOT_FW_AMPDU_RX  1   RX A-MPDU + reorder buffer (8)
#   CONFIG_BFLB_IOT_FW_AMSDU     1   A-MSDU aggregation (implies AGG)
#   CONFIG_BFLB_IOT_FW_CHAN_WIDTH 0  CFG_FW_CHAN_WIDTH
#   CONFIG_BFLB_IOT_FW_P2P       0   P2P GO
#   CONFIG_BFLB_IOT_FW_DBG       0   CFG_DBG=2 vs CFG_DBG=0
#   CONFIG_BFLB_IOT_FW_CONN_DIAG_TLV 0  SM_CONN_DIAG_TLV
#   CONFIG_TD_DIAGNOSIS_STA      0
#   CONFIG_PHY_BZ_ONLY           0   CFG_BZ_ONLY=1 (Bluetooth/Zigbee-only build)
#   CONFIG_RX_FLOW_CNTRL_DISABLE 0
#   CONFIG_MATTER_SUPPORT        0
#   CONFIG_MAC_TXQ_DEPTH         (required from parent — proj.conf sets 4 on BL602)
#   CONFIG_MAC_RXQ_DEPTH         (required from parent — proj.conf sets 4 on BL602)

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR "wifi4_firmware: only BL602 is supported (BOUFFALO_CHIP=${BOUFFALO_CHIP})")
endif()

set(CONFIG_STA_MAX                 5  CACHE STRING "wifi4: max STAs on soft-AP")
set(CONFIG_VIRT_DEV_MAX            2  CACHE STRING "wifi4: virtual interface count")
set(CONFIG_BFLB_IOT_FW_AMPDU       1  CACHE STRING "wifi4: TX A-MPDU aggregation")
set(CONFIG_BFLB_IOT_FW_AMPDU_RX    1  CACHE STRING "wifi4: RX A-MPDU + reorder buffer")
set(CONFIG_BFLB_IOT_FW_AMSDU       1  CACHE STRING "wifi4: A-MSDU aggregation")
set(CONFIG_BFLB_IOT_FW_CHAN_WIDTH  0  CACHE STRING "wifi4: CFG_FW_CHAN_WIDTH")
set(CONFIG_BFLB_IOT_FW_P2P         0  CACHE STRING "wifi4: P2P GO")
set(CONFIG_BFLB_IOT_FW_DBG         0  CACHE STRING "wifi4: firmware debug level")
set(CONFIG_BFLB_IOT_FW_CONN_DIAG_TLV 0 CACHE STRING "wifi4: SM_CONN_DIAG_TLV")
set(CONFIG_TD_DIAGNOSIS_STA        0  CACHE STRING "wifi4: TD_DIAGNOSIS_STA")
set(CONFIG_PHY_BZ_ONLY             0  CACHE STRING "wifi4: PHY Bluetooth/Zigbee-only")
set(CONFIG_RX_FLOW_CNTRL_DISABLE   0  CACHE STRING "wifi4: disable RX flow control")
set(CONFIG_MATTER_SUPPORT          0  CACHE STRING "wifi4: Matter support shims")

foreach(_v MAC_TXQ_DEPTH MAC_RXQ_DEPTH)
    if(NOT DEFINED CONFIG_${_v})
        message(FATAL_ERROR "wifi4_firmware: CONFIG_${_v} not set in parent scope")
    endif()
endforeach()

add_library(wifi4_firmware INTERFACE)

target_include_directories(wifi4_firmware INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(wifi4_firmware INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libfirmware.a"
)

target_compile_definitions(wifi4_firmware INTERFACE
    # BL602-only base block (the upstream gates these on CHIP==bl602).
    CFG_BOUFFALO
    CFG_EMB
    CFG_UMAC
    CFG_SPC=16
    CFG_AMSDU_4K
    CFG_TXDESC0=2
    CFG_TXDESC1=2
    CFG_TXDESC2=2
    CFG_TXDESC3=2
    CFG_MDM_VER_V21
    CFG_IPC_VER_V11
    CFG_BFMEE
    CFG_CMON
    CFG_HWSCAN
    CFG_KEYCFG
    CFG_BWLEN
    CFG_REC
    CFG_HSU=2
    CFG_DEFAULT_AC=0
    # Chip-agnostic base block.
    CFG_BCN
    CFG_APM_EMBEDDED
    CFG_AUTOBCN
    CFG_STA_MAX=${CONFIG_STA_MAX}
    CFG_TXDESC4=4
    MICRO_AP_MODE
    AUTHENTICATOR
    AP_WPA
    CFG_PHY_RELEASE=1
    CFG_VIRT_DEV_MAX=${CONFIG_VIRT_DEV_MAX}
    CONFIG_MAC_TXQ_DEPTH=${CONFIG_MAC_TXQ_DEPTH}
    CONFIG_MAC_RXQ_DEPTH=${CONFIG_MAC_RXQ_DEPTH}
    __FILENAME__=__FILE__
    CFG_CHIP_BL602
    # sdk_add_compile_options block — these are -D… flags on options(),
    # which Bouffalo's wrapper feeds into the same per-target compile-defs
    # path. Mirroring them as plain definitions here.
    BL602_USE_HAL_DRIVER
    ARCH_RISCV
    RW_WAPI_EN=0
    RW_BFMEE_EN=0
    RW_WLAN_COEX_EN=1
    RW_MUMIMO_RX_EN=0
    RW_NX_LDPC_DEC=1
    CFG_PS
    CFG_DPSM
    NX_POWERSAVE=1
    CFG_UAPSD
    RF_IN_SDK
)

if(CONFIG_BFLB_IOT_FW_AMPDU EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE
        CFG_AGG
        CFG_BATX=2
        CFG_KE_MEMHEAP
    )
endif()

if(CONFIG_BFLB_IOT_FW_CHAN_WIDTH EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE CFG_FW_CHAN_WIDTH)
endif()

if(CONFIG_BFLB_IOT_FW_AMPDU_RX EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE
        CFG_BARX=2
        CFG_REORD_BUF=8
    )
else()
    target_compile_definitions(wifi4_firmware INTERFACE CFG_BARX=0)
endif()

if(CONFIG_BFLB_IOT_FW_AMSDU EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE CFG_AMSDU)
    # AMSDU implies AGG; if AMPDU already pulled CFG_AGG in we still get it
    # once (it's a -D macro, redundancy is harmless).
    if(NOT CONFIG_BFLB_IOT_FW_AMPDU EQUAL 1)
        target_compile_definitions(wifi4_firmware INTERFACE
            CFG_AGG
            CFG_BATX=2
        )
    endif()
endif()

if(CONFIG_BFLB_IOT_FW_DBG EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE CFG_DBG=2)
else()
    target_compile_definitions(wifi4_firmware INTERFACE CFG_DBG=0)
endif()

if(CONFIG_BFLB_IOT_FW_P2P EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE
        CFG_P2P=1
        CFG_P2P_GO
    )
else()
    target_compile_definitions(wifi4_firmware INTERFACE CFG_P2P=0)
endif()

if(CONFIG_RX_FLOW_CNTRL_DISABLE EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE CFG_RX_FLOW_CNTRL_DISABLE)
endif()

if(CONFIG_BFLB_IOT_FW_CONN_DIAG_TLV EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE SM_CONN_DIAG_TLV)
endif()

if(CONFIG_TD_DIAGNOSIS_STA EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE TD_DIAGNOSIS_STA)
endif()

if(CONFIG_PHY_BZ_ONLY EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE
        PHYRF_LOG_EN=0
        CFG_BZ_ONLY=1
    )
else()
    target_compile_definitions(wifi4_firmware INTERFACE
        PHYRF_LOG_EN=0
        CFG_BZ_ONLY=0
    )
endif()

if(CONFIG_MATTER_SUPPORT EQUAL 1)
    target_compile_definitions(wifi4_firmware INTERFACE MATTER_SUPPORT)
endif()
