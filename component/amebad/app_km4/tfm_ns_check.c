/*
 * tfm_ns_check.c — minimal compile/link probe of the TF-M NS interface.
 *
 * When AMEBAD_TFM_BOOT is on, app_km4 is TF-M's Non-Secure side. This TU only
 * proves the in-tree wiring holds at compile + link time:
 *   - the PSA client headers exported by the TF-M build are on the include path;
 *   - the NS-callable veneers in s_veneers.o resolve at link.
 * It does NOT issue a real PSA call — full NSPE integration (psa_call to a
 * secure service, NS memory/vector layout, the bootloader BLXNS landing on
 * app_km4) is a follow-on stage. The probe is unreferenced and harmless (the KM4
 * image2 link has no --gc-sections, so it simply rides along).
 */
#include <stdint.h>
#include "psa/client.h"
#include "psa/error.h"

/* TF-M exports this NS-callable veneer via s_veneers.o (an SG entry at a fixed
 * secure address). Referencing it pulls s_veneers.o into the link. */
extern uint32_t tfm_psa_framework_version_veneer(void);

uint32_t amebad_tfm_ns_probe(void)
{
    return tfm_psa_framework_version_veneer();
}
