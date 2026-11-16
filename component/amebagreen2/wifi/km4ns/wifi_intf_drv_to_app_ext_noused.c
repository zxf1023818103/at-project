/* WHC checks this marker after an IPC call. Unlike SDK builds, this project
 * keeps the complete NP API: KM4NS must not contain stubs generated from an old
 * KM4TZ ELF, since feature selections and callers can change independently. */
#include <stdint.h>
uint16_t call_noused;
