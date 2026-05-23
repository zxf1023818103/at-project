# bflb_soc — per-chip startup + SoC drivers + PHY/RF
# (extracted from platform/bouffalo_sdk/drivers/soc).
#
# SDK forwarder picks ${CHIP}/std and (optionally) ${CHIP}/phyrf by passing in
# CONFIG_RF / CONFIG_PHY_LPFW.

# This file is reached only if SDK forwarder add_subdirectory's it. The actual
# chip lib is selected via the dedicated sub-CMakeLists in <chip>/std and <chip>/phyrf.
# We don't aggregate here — leave the choice of which add_subdirectory(${CHIP}/std)
# to call to the SDK forwarder, so non-applicable chip dirs are never visited.
