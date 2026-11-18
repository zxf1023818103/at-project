/* Exercise actual flash-backed PSA storage: large records, replacement, partial
 * reads, deletion, write-once enforcement and persistence across hardware reset. */
#include "FreeRTOS.h"
#include "psa/internal_trusted_storage.h"
#include "psa/protected_storage.h"
#include <stdio.h>
#include <string.h>
static const psa_storage_uid_t persistent = 0x44504c5553504552ULL;
static const psa_storage_uid_t scratch = 0x44504c5553544553ULL;
static const psa_storage_uid_t once = 0x44504c55534f4e43ULL;

static void exercise(int ps)
{
    static uint8_t value[4097], output[4097];
    const size_t persistent_size = 1537;
    const size_t maximum = ps ? 4096 : 2048;
    size_t actual = 0;
    for (unsigned i=0; i<sizeof(value); ++i) value[i] = (uint8_t)(i * 73 + 29);
    psa_status_t (*set)(psa_storage_uid_t,size_t,const void *,psa_storage_create_flags_t) = ps ? psa_ps_set : psa_its_set;
    psa_status_t (*get)(psa_storage_uid_t,size_t,size_t,void *,size_t *) = ps ? psa_ps_get : psa_its_get;
    psa_status_t (*remove)(psa_storage_uid_t) = ps ? psa_ps_remove : psa_its_remove;
    psa_status_t (*info)(psa_storage_uid_t,struct psa_storage_info_t *) = ps ? psa_ps_get_info : psa_its_get_info;
    const char *label = ps ? "PS" : "ITS";
    struct psa_storage_info_t metadata;
    int rc = info(persistent, &metadata);
    int first = rc == PSA_ERROR_DOES_NOT_EXIST;
    if (first) configASSERT(set(persistent,persistent_size,value,0) == PSA_SUCCESS);
    else configASSERT(rc == PSA_SUCCESS && metadata.size == persistent_size);
    configASSERT(get(persistent,0,sizeof(output),output,&actual) == PSA_SUCCESS);
    configASSERT(actual == persistent_size && !memcmp(value,output,actual));
    printf("[STORAGE] %s %s PASS (%u bytes)\n",label,first ? "first write/read" : "reset persistence",(unsigned)actual);
    for (unsigned iteration = 0; iteration < 5; ++iteration) {
        value[0] = iteration;
        configASSERT(set(scratch,maximum,value,0) == PSA_SUCCESS);
        configASSERT(get(scratch,7,519,output,&actual) == PSA_SUCCESS && actual == 519);
        configASSERT(!memcmp(value+7,output,actual));
        configASSERT(get(scratch,0,sizeof(output),output,&actual) == PSA_SUCCESS && actual == maximum);
        configASSERT(!memcmp(value,output,actual));
    }
    configASSERT(set(scratch,maximum+1,value,0) != PSA_SUCCESS);
    configASSERT(get(scratch,0,sizeof(output),output,&actual) == PSA_SUCCESS && actual == maximum);
    configASSERT(!memcmp(value,output,actual));
    configASSERT(remove(scratch) == PSA_SUCCESS);
    configASSERT(info(scratch,&metadata) == PSA_ERROR_DOES_NOT_EXIST);
    rc = info(once,&metadata);
    if (rc == PSA_ERROR_DOES_NOT_EXIST) configASSERT(set(once,16,value,PSA_STORAGE_FLAG_WRITE_ONCE) == PSA_SUCCESS);
    else configASSERT(rc == PSA_SUCCESS);
    configASSERT(set(once,16,value,0) == PSA_ERROR_NOT_PERMITTED);
    configASSERT(remove(once) == PSA_ERROR_NOT_PERMITTED);
    printf("[STORAGE] %s max=%u overwrite/partial-read/oversize/delete/write-once PASS\n",label,(unsigned)maximum);
}
void green2_storage_test(void) { exercise(0); exercise(1); }
