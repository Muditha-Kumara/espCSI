/*
 * Session Store - Receiver
 * FATFS-based CSV session file management
 */

#include "session_store.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "wear_levelling.h"
#include "diskio_wl.h"
#include "diskio_impl.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "session_store";

#define MOUNT_POINT "/data"
#define MAX_OPEN_FILE 1

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static FILE *s_current_file = NULL;
static char s_current_id[SESSION_ID_LEN + 1] = {0};
static char s_current_filename[128] = {0};
static bool s_active = false;

esp_err_t session_store_init(void)
{
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };

    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_POINT, "storage",
                                                      &mount_config, &s_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "FATFS mounted at %s", MOUNT_POINT);
    return ESP_OK;
}

esp_err_t session_store_begin(const char *label, uint8_t channel, uint16_t rate_hz)
{
    if (s_active) {
        ESP_LOGW(TAG, "Session already active");
        return ESP_ERR_INVALID_STATE;
    }

    // Generate session ID from timestamp
    int64_t now = esp_timer_get_time();
    snprintf(s_current_id, sizeof(s_current_id), "%016llx", (unsigned long long)now);

    // Create filename: label_channel_rate_id.csv
    snprintf(s_current_filename, sizeof(s_current_filename),
             "%s/%s_ch%d_%dhz_%s.csv", MOUNT_POINT, label, channel, rate_hz, s_current_id);

    s_current_file = fopen(s_current_filename, "w");
    if (s_current_file == NULL) {
        ESP_LOGE(TAG, "Failed to create session file: %s", s_current_filename);
        return ESP_FAIL;
    }

    // Write CSV header
    fprintf(s_current_file,
        "CSI_DATA,id,mac,rssi,rate,noise_floor,channel,timestamp,sig_len,rx_format,len,first_word,data\n");

    s_active = true;
    ESP_LOGI(TAG, "Session started: %s", s_current_filename);
    return ESP_OK;
}

esp_err_t session_store_append(const char *csv_line, int len)
{
    if (!s_active || s_current_file == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t written = fwrite(csv_line, 1, len, s_current_file);
    if ((int)written != len) {
        ESP_LOGE(TAG, "Write failed: wrote %d of %d", (int)written, len);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t session_store_end(void)
{
    if (!s_active || s_current_file == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    fflush(s_current_file);
    fclose(s_current_file);
    s_current_file = NULL;
    s_active = false;

    ESP_LOGI(TAG, "Session ended: %s", s_current_filename);
    s_current_id[0] = '\0';
    s_current_filename[0] = '\0';
    return ESP_OK;
}

bool session_store_is_active(void)
{
    return s_active;
}

const char *session_store_get_id(void)
{
    if (!s_active) return NULL;
    return s_current_id;
}

esp_err_t session_store_list(char *buf, size_t buf_size)
{
    DIR *dir = opendir(MOUNT_POINT);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory");
        return ESP_FAIL;
    }

    int offset = snprintf(buf, buf_size, "[");
    bool first = true;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Only list .csv files
        size_t name_len = strlen(entry->d_name);
        if (name_len < 4 || strcmp(entry->d_name + name_len - 4, ".csv") != 0) {
            continue;
        }

        // Get file size
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, entry->d_name);
        struct stat st;
        stat(full_path, &st);

        if (!first) {
            offset += snprintf(buf + offset, buf_size - offset, ",");
        }
        offset += snprintf(buf + offset, buf_size - offset,
                          "{\"name\":\"%s\",\"size\":%ld}",
                          entry->d_name, (long)st.st_size);
        first = false;
    }

    snprintf(buf + offset, buf_size - offset, "]");
    closedir(dir);
    return ESP_OK;
}

esp_err_t session_store_delete(const char *filename)
{
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, filename);

    if (unlink(full_path) != 0) {
        ESP_LOGE(TAG, "Failed to delete: %s", full_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleted: %s", full_path);
    return ESP_OK;
}

esp_err_t session_store_get_path(const char *filename, char *path, size_t path_size)
{
    snprintf(path, path_size, "%s/%s", MOUNT_POINT, filename);

    // Verify file exists
    struct stat st;
    if (stat(path, &st) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

uint64_t session_store_get_free_space(void)
{
    FATFS *fs;
    DWORD free_clust;
    FRESULT res = f_getfree(MOUNT_POINT, &free_clust, &fs);
    if (res != FR_OK) {
        ESP_LOGE(TAG, "Failed to get free space: %d", res);
        return 0;
    }
    uint64_t free_bytes = (uint64_t)free_clust * fs->csize * CONFIG_WL_SECTOR_SIZE;
    return free_bytes;
}
