#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Byte access helpers from newer CherryUSB, absent in the SDK core headers. */
#ifndef GET_LE16
static inline uint16_t GET_LE16(const void *p)
{const uint8_t *b=p;return b[0]|((uint16_t)b[1]<<8);}
static inline uint32_t GET_LE32(const void *p)
{const uint8_t *b=p;return GET_LE16(b)|((uint32_t)GET_LE16(b+2)<<16);}
static inline void SET_LE16(void *p,uint16_t value)
{uint8_t *b=p;b[0]=value;b[1]=value>>8;}
static inline void SET_LE32(void *p,uint32_t value)
{uint8_t *b=p;SET_LE16(b,value);SET_LE16(b+2,value>>16);}
#endif
void ncm_tcp_init(void);
void ncm_tcp_usb_event(unsigned event);
void usbd_cdc_ncm_link_changed(bool active);
void ncm_tcp_status(void);
