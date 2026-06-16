/*
 * aws_iot_config.h — non-secret build-time constants for the AmebaD KM4 AWS IoT
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

/* Amazon Root CA 1 (public; server-cert trust anchor for *.iot.<region>.amazonaws.com).
 * Fetched from https://www.amazontrust.com/repository/AmazonRootCA1.pem */
#define AWS_ROOT_CA_PEM \
    "-----BEGIN CERTIFICATE-----\n" \
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n" \
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n" \
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n" \
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n" \
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n" \
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n" \
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n" \
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n" \
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n" \
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n" \
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n" \
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n" \
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n" \
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n" \
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n" \
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n" \
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n" \
    "rqXRfboQnoZsG4q5WTP468SQvvG5\n" \
    "-----END CERTIFICATE-----\n" \
    ""

#endif /* AWS_IOT_CONFIG_H */
