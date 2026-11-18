/*
 * aws_telemetry_common.h - ONE telemetry topic + JSON schema for every platform/variant.
 *
 * Both the lean client (aws_telemetry.c, bl602 AWSOTA=OFF) and the agent client (aws_iot_demo.c,
 * AmebaD + bl602 AWSOTA=ON) include this so they publish to the SAME topic (AWS_TOPIC_TELEMETRY_FMT
 * = bw20/<mac>/telemetry, the topic the CloudWatch rule matches) with the SAME payload schema
 * {"thing","fw","seq","uptime_ms","heap"}. Header-only (static inline) so no extra link wiring; the
 * including TU already pulls FreeRTOS (tick + heap) — aws_iot_config.h supplies the topic macro.
 */
#ifndef AWS_TELEMETRY_COMMON_H
#define AWS_TELEMETRY_COMMON_H

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "aws_iot_config.h"      /* AWS_TOPIC_TELEMETRY_FMT (bw20/%s/telemetry) */

#ifndef AT_FW_VERSION
#define AT_FW_VERSION "dev"
#endif

/* Build the shared telemetry JSON into buf. Returns snprintf's count (>0 on success). */
static inline int aws_build_telemetry_json(char *buf, size_t len, const char *mac, unsigned seq)
{
    return snprintf(buf, len,
                    "{\"thing\":\"%s\",\"fw\":\"%s\",\"seq\":%u,\"uptime_ms\":%u,\"heap\":%u}",
                    mac, AT_FW_VERSION, seq,
                    (unsigned) (xTaskGetTickCount() * portTICK_PERIOD_MS),
                    (unsigned) xPortGetFreeHeapSize());
}

#endif /* AWS_TELEMETRY_COMMON_H */
