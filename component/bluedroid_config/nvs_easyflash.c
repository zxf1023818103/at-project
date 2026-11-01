#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "easyflash.h"

/* The Bluedroid config backend uses one namespace and serializes access with
 * its own mutex. Keep a separate key prefix from the application's settings. */
struct namespace { char name[NVS_NS_NAME_MAX_SIZE]; };

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out)
{
    if (!name || !out || mode != NVS_READWRITE || strlen(name) >= NVS_NS_NAME_MAX_SIZE)
        return ESP_ERR_INVALID_ARG;
    struct namespace *space = malloc(sizeof(*space));
    if (!space) return ESP_ERR_NO_MEM;
    strcpy(space->name, name);
    *out = (uintptr_t)space;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) { free((void *)handle); }

static bool make_key(nvs_handle_t handle, const char *name, char key[EF_ENV_NAME_MAX])
{
    if (!handle || !name) return false;
    struct namespace *space = (void *)handle;
    int count = snprintf(key, EF_ENV_NAME_MAX, "bd_%s_%s", space->name, name);
    return count > 0 && count < EF_ENV_NAME_MAX;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *name, void *data, size_t *length)
{
    char key[EF_ENV_NAME_MAX];
    if (!length || !make_key(handle, name, key)) return ESP_ERR_INVALID_ARG;
    size_t saved = 0;
    ef_get_env_blob(key, NULL, 0, &saved);
    if (!saved) return ESP_ERR_NVS_NOT_FOUND;
    if (!data) { *length = saved; return ESP_OK; }
    if (*length < saved) { *length = saved; return ESP_ERR_NVS_INVALID_LENGTH; }
    size_t got = ef_get_env_blob(key, data, *length, &saved);
    *length = saved;
    return got == saved ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *name, const void *data, size_t length)
{
    char key[EF_ENV_NAME_MAX];
    if ((!data && length) || !make_key(handle, name, key)) return ESP_ERR_INVALID_ARG;
    /* NVS accepts an empty blob; Bluedroid writes one for an empty config.
     * EasyFlash rejects zero-length writes, so represent it as no key. */
    if (!length) return ef_del_env(key) == EF_NO_ERR ? ESP_OK : ESP_FAIL;
    return ef_set_env_blob(key, data, length) == EF_NO_ERR ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return ef_save_env() == EF_NO_ERR ? ESP_OK : ESP_FAIL;
}
