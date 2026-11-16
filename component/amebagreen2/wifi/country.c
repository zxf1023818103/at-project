/* Shared 802.11d policy over the Green2 scan result's Country IE. */
#include "wifi_api.h"
#include "wifi_api_ext.h"
#include "FreeRTOS.h"
#include "dot11d.h"
#include <stdlib.h>
#include <stdio.h>

int dot11d_port_scan(dot11d_tally_t *tally)
{
    uint32_t count = 32;
    struct rtw_scan_result *records = pvPortMalloc(count * sizeof(*records));
    if (!records) return -1;
    struct rtw_scan_param params = {0};
    params.max_ap_record_num = count;
    int rc = wifi_scan_networks(&params, 1);
    if (rc >= 0) rc = wifi_get_scan_records(&count, records);
    if (rc >= 0) for (uint32_t i=0; i<count && i<32; ++i)
        dot11d_tally_add(tally, records[i].country_code[0], records[i].country_code[1]);
    vPortFree(records);
    return rc < 0 ? -1 : 0;
}

void dot11d_port_apply(const char cc[3])
{
    uint8_t country[2] = {(uint8_t)cc[0], (uint8_t)cc[1]};
    printf("[802.11d] country %.2s result=%ld\n", cc, (long)wifi_set_countrycode(country));
}

void amebagreen2_country_init(void)
{
    const char *cc = getenv("WIFI_COUNTRY");
    if (dot11d_cc_valid(cc)) dot11d_port_apply(cc);
    else dot11d_autodetect_country();
}
