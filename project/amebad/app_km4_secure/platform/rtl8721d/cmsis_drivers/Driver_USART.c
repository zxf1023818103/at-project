/*
 * RTL8721D KM4 — minimal CMSIS Driver_USART0 for TF-M secure logging.
 * Send() routes to the on-chip ROM LOGUART (UART2 @0x48012000, already brought
 * up + baud-configured by the Ameba boot chain before TF-M runs); the rest are
 * minimal stubs sufficient for tfm_hal stdio.
 */
#include "Driver_USART.h"

/* AmebaD ROM LOGUART_PutChar. The ROM exposes TWO aliases: a Non-Secure one
 * (0x1010caf5, in NS ROM 0x1010A000+) and a SECURE one (0x10102af5, in secure
 * ROM 0x10100000-0x1010A000 — the alias the secure bootloader itself uses). TF-M
 * runs in the Secure state, so it must call the SECURE alias; a direct branch
 * from Secure to an NS ROM address would fault. (Thumb bit already set.) */
#define AMEBAD_ROM_LOGUART_PUTCHAR_S  0x10102af5u
static inline void LOGUART_PutChar(unsigned char c)
{
    ((void (*)(unsigned char))AMEBAD_ROM_LOGUART_PUTCHAR_S)(c);
}

#define ARM_USART_DRV_VERSION ARM_DRIVER_VERSION_MAJOR_MINOR(2, 0)

static const ARM_DRIVER_VERSION DriverVersion = {
    ARM_USART_API_VERSION, ARM_USART_DRV_VERSION
};
static const ARM_USART_CAPABILITIES DriverCapabilities = { 0 };

static ARM_DRIVER_VERSION ARM_USART_GetVersion(void)       { return DriverVersion; }
static ARM_USART_CAPABILITIES ARM_USART_GetCapabilities(void) { return DriverCapabilities; }
static int32_t ARM_USART_Initialize(ARM_USART_SignalEvent_t cb) { (void)cb; return ARM_DRIVER_OK; }
static int32_t ARM_USART_Uninitialize(void)                { return ARM_DRIVER_OK; }
static int32_t ARM_USART_PowerControl(ARM_POWER_STATE s)   { (void)s; return ARM_DRIVER_OK; }

static int32_t ARM_USART_Send(const void *data, uint32_t num)
{
    const unsigned char *p = (const unsigned char *)data;
    for (uint32_t i = 0; i < num; i++) {
        LOGUART_PutChar(p[i]);
    }
    return ARM_DRIVER_OK;
}

static int32_t ARM_USART_Receive(void *data, uint32_t num)             { (void)data; (void)num; return ARM_DRIVER_ERROR_UNSUPPORTED; }
static int32_t ARM_USART_Transfer(const void *t, void *r, uint32_t n)  { (void)t; (void)r; (void)n; return ARM_DRIVER_ERROR_UNSUPPORTED; }
static uint32_t ARM_USART_GetTxCount(void)                            { return 0; }
static uint32_t ARM_USART_GetRxCount(void)                            { return 0; }
static int32_t ARM_USART_Control(uint32_t ctrl, uint32_t arg)         { (void)ctrl; (void)arg; return ARM_DRIVER_OK; }
static ARM_USART_STATUS ARM_USART_GetStatus(void)                    { ARM_USART_STATUS s = {0}; return s; }
static int32_t ARM_USART_SetModemControl(ARM_USART_MODEM_CONTROL c)   { (void)c; return ARM_DRIVER_ERROR_UNSUPPORTED; }
static ARM_USART_MODEM_STATUS ARM_USART_GetModemStatus(void)         { ARM_USART_MODEM_STATUS s = {0}; return s; }

ARM_DRIVER_USART Driver_USART0 = {
    ARM_USART_GetVersion,
    ARM_USART_GetCapabilities,
    ARM_USART_Initialize,
    ARM_USART_Uninitialize,
    ARM_USART_PowerControl,
    ARM_USART_Send,
    ARM_USART_Receive,
    ARM_USART_Transfer,
    ARM_USART_GetTxCount,
    ARM_USART_GetRxCount,
    ARM_USART_Control,
    ARM_USART_GetStatus,
    ARM_USART_SetModemControl,
    ARM_USART_GetModemStatus,
};
