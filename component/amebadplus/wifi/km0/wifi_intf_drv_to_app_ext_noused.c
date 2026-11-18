/* WHC checks this marker after an IPC call. Unlike SDK builds, this project
 * keeps the complete NP API: KM0 must not contain stubs generated from an old
 * KM4 ELF, since feature selections and callers can change independently. */
#include <stdint.h>
uint16_t call_noused;
