/*
 * aws_iot_config.h - non-secret build-time constants for the AmebaD KM4 AWS IoT
 * Core MQTT/TLS demo (make AWS=ON). The broker endpoint, client certificate and
 * private key are NOT here: the endpoint + cert are provisioned to LittleFS and
 * the private key to TF-M Protected Storage via the `set` console command
 * (see aws_provision.c). Only public, fixed values live here.
 */
#ifndef AWS_IOT_CONFIG_H
#define AWS_IOT_CONFIG_H

/* AWS IoT Core MQTT over mutually-authenticated TLS. Port 8883 (X.509 client
 * auth); no ALPN needed (ALPN "x-amzn-mqtt-ca" is only for port 443). */
#define AWS_MQTT_PORT            8883

/* MQTT keep-alive (s) and the demo telemetry publish period (ms). */
#define AWS_MQTT_KEEPALIVE_S     60
#define AWS_TELEMETRY_PERIOD_MS  5000

/* Topic layout: bw20/<thingname>/telemetry (device->cloud, also matched by the
 * CloudWatch topic rule "bw20/+/telemetry") and bw20/<thingname>/cmd (cloud->device).
 * <thingname> = the module BLE MAC (lowercase hex, e.g. 112233aabbcc). */
#define AWS_TOPIC_PREFIX         "bw20"
#define AWS_TOPIC_TELEMETRY_FMT  AWS_TOPIC_PREFIX "/%s/telemetry"
#define AWS_TOPIC_CMD_FMT        AWS_TOPIC_PREFIX "/%s/cmd"

/* Server-cert trust comes from the `ca/` folder on LittleFS (shipped from project/resource/ca)
 * plus the embedded Mozilla CA bundle in a CRT_BUNDLE=ON build -- no root CA is baked into the
 * source. See NetworkCredentials_t.pRootCaPath. */

#endif /* AWS_IOT_CONFIG_H */
