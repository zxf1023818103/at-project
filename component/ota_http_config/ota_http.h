/*
 * ota_http.h - programmatic entry points for the `ota_http` OTA downloader (see ota_http.c).
 *
 * ota_http streams an OTA_All.bin over HTTP/HTTPS (single GET, llhttp, shared Https_Connect) into the
 * inactive OTA slot and reboots on success. Besides the `ota_http <url>` console command, the AWS OTA
 * path drives it programmatically: ota_app.c's prvOtaHttpInit hands the job-doc presigned S3 URL to
 * ota_http_start(), so an AWS OTA job just delivers the URL while ota_http does the download+apply.
 */
#ifndef OTA_HTTP_H
#define OTA_HTTP_H

/* Register the `ota_http <url>` FreeRTOS-CLI console command. */
void ota_http_register( void );

/* Launch the async downloader for `url` (an internal private copy is made, so the caller keeps
 * ownership of its buffer). Non-blocking: returns once the worker task is created. The worker
 * reboots the device on a successful update and just logs+exits on failure (no reboot). */
void ota_http_start( const char * url );

/* Optional one-shot failure hook, fired (from the worker task, or synchronously from
 * ota_http_start on a task-create failure) when the download does NOT end in the success-reboot.
 * The AWS HTTP-takeover path (ota_app.c) uses it to clear its re-download guard and reboot back
 * into a clean MQTT session - by takeover time the MQTT agent has been torn down and a failed
 * download would otherwise leave the device wedged. Cleared after firing; the console command
 * path never sets it (a manual ota_http failure just logs). */
void ota_http_set_fail_hook( void ( * hook )( void ) );

#endif /* OTA_HTTP_H */
