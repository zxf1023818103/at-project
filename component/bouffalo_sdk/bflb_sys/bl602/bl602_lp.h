/* bl602_lp.h — stub.
 *
 * The upstream Bouffalo SDK doesn't ship a bl602_lp.h: BL602 predates the
 * LP-framework introduced for BL616. However, bl_lp.h does a chip-keyed
 * #include "bl602_lp.h" on BL602, and several files we still compile on
 * BL602 (bflb_power_mgmt's pm_manager / clock_manager / tickless*) pull
 * in bl_lp.h transitively.
 *
 * This stub exists only so those #includes resolve. None of the LP API
 * symbols are referenced by code that actually runs on BL602 — those code
 * paths are dead on this chip. If something does try to link bl_lp_*()
 * on BL602, you'll get an undefined-reference at link time, which is the
 * right outcome (forcing whoever added the call to gate it correctly).
 */
#ifndef __BL602_LP_H__
#define __BL602_LP_H__

#include <stdint.h>
#include <stdbool.h>

#endif /* __BL602_LP_H__ */
