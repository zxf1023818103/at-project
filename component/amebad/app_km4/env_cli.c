/*
 * env_cli.c — generic environment-variable console commands (setenv / getenv / env)
 * over newlib's environ, for the AmebaD KM4 FreeRTOS-Plus-CLI.
 *
 * Registered from main() after amebad_monitor_start() (like fs_cli.c / time_cli.c), so they
 * are in EVERY KM4 console image, not just AWS. Moved here out of aws_cli.c so e.g. the SNTP
 * image can `setenv NTP_SERVER <host>` then `sntp_update`.
 *
 * Persistence is a link-time property of the image, transparent to this file: the AWS image
 * links -Wl,--wrap=setenv (syscall_config/env.c) so a setenv here auto-persists to KV/<name>
 * and reloads on boot; other images get libc's plain setenv (RAM-only, lost on reboot).
 * Names/values are single space-free CLI tokens (the CLI splits arguments on spaces).
 */
#include <stdio.h>
#include <stdlib.h>          /* setenv / getenv / unsetenv */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

#define ENV_CLI_NAME_MAX   64
#define ENV_CLI_VALUE_MAX  256

static BaseType_t cmd_setenv(char *out, size_t outlen, const char *cmd)
{
    BaseType_t ln = 0, lv = 0;
    const char *name = FreeRTOS_CLIGetParameter(cmd, 1, &ln);
    const char *val  = FreeRTOS_CLIGetParameter(cmd, 2, &lv);
    char nbuf[ENV_CLI_NAME_MAX], vbuf[ENV_CLI_VALUE_MAX];

    if (name == NULL || ln == 0 || (size_t)ln >= sizeof(nbuf)) {
        snprintf(out, outlen, "usage: setenv <name> <value>  (empty value unsets)\r\n");
        return pdFALSE;
    }
    memcpy(nbuf, name, (size_t)ln);
    nbuf[ln] = '\0';
    if (val == NULL || lv == 0) {                 /* no value -> unset */
        unsetenv(nbuf);
        snprintf(out, outlen, "unset %s\r\n", nbuf);
        return pdFALSE;
    }
    if ((size_t)lv >= sizeof(vbuf)) {
        snprintf(out, outlen, "setenv: value too long (max %u)\r\n",
                 (unsigned)(sizeof(vbuf) - 1));
        return pdFALSE;
    }
    memcpy(vbuf, val, (size_t)lv);
    vbuf[lv] = '\0';
    snprintf(out, outlen, "setenv %s=%s -> %s\r\n", nbuf, vbuf,
             (setenv(nbuf, vbuf, 1) == 0) ? "OK" : "FAIL");
    return pdFALSE;
}

static BaseType_t cmd_getenv(char *out, size_t outlen, const char *cmd)
{
    BaseType_t ln = 0;
    const char *name = FreeRTOS_CLIGetParameter(cmd, 1, &ln);
    const char *v;
    char nbuf[ENV_CLI_NAME_MAX];

    if (name == NULL || ln == 0 || (size_t)ln >= sizeof(nbuf)) {
        snprintf(out, outlen, "usage: getenv <name>\r\n");
        return pdFALSE;
    }
    memcpy(nbuf, name, (size_t)ln);
    nbuf[ln] = '\0';
    v = getenv(nbuf);
    snprintf(out, outlen, "%s=%s\r\n", nbuf, v ? v : "");
    return pdFALSE;
}

/* List environ one entry per FreeRTOS+CLI call (return pdTRUE => call again), so a long
 * environment isn't truncated by the single CLI output buffer. */
static BaseType_t cmd_env(char *out, size_t outlen, const char *cmd)
{
    extern char **environ;
    static int idx = 0;
    (void)cmd;

    if (environ == NULL || environ[idx] == NULL) {
        idx = 0;
        out[0] = '\0';
        return pdFALSE;                 /* nothing (more) to print */
    }
    snprintf(out, outlen, "%s\r\n", environ[idx]);
    idx++;
    if (environ[idx] == NULL) {
        idx = 0;
        return pdFALSE;                 /* that was the last entry */
    }
    return pdTRUE;                       /* call again for the next entry */
}

void env_cli_register(void)
{
    static const CLI_Command_Definition_t cmd_setenv_def = {
        "setenv", "setenv <name> <value>:\r\n set an env var (empty value unsets; persisted in the AWS image)\r\n",
        cmd_setenv, -1
    };
    static const CLI_Command_Definition_t cmd_getenv_def = {
        "getenv", "getenv <name>:\r\n print an env var\r\n",
        cmd_getenv, 1
    };
    static const CLI_Command_Definition_t cmd_env_def = {
        "env", "env:\r\n list all env vars\r\n",
        cmd_env, 0
    };

    FreeRTOS_CLIRegisterCommand(&cmd_setenv_def);
    FreeRTOS_CLIRegisterCommand(&cmd_getenv_def);
    FreeRTOS_CLIRegisterCommand(&cmd_env_def);
}
