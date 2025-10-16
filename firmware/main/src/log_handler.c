#include "log_handler.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include <sys/stat.h>
#include <inttypes.h>

static FILE *log_file = NULL;
static uint32_t boot_count = 0;
static SemaphoreHandle_t log_mutex = NULL;
static const char *TAG = "LOG_HANDLER";

static const char* get_reset_reason_string(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN:    return "UNKNOWN";
        case ESP_RST_POWERON:    return "POWER_ON";
        case ESP_RST_EXT:        return "EXTERNAL_PIN";
        case ESP_RST_SW:         return "SOFTWARE";
        case ESP_RST_PANIC:      return "PANIC/EXCEPTION";
        case ESP_RST_INT_WDT:    return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT:   return "TASK_WATCHDOG";
        case ESP_RST_WDT:        return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP:  return "DEEP_SLEEP";
        case ESP_RST_BROWNOUT:   return "BROWNOUT";
        case ESP_RST_SDIO:       return "SDIO";
        default:                 return "UNDEFINED";
    }
}

esp_err_t log_handler_init(void)
{
    log_mutex = xSemaphoreCreateMutex();
    if (log_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create log mutex");
        return ESP_FAIL;
    }
    
    // Initialize SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB used", total / 1024, used / 1024);
    }
    
    // Get reset reason and boot count
    esp_reset_reason_t reset_reason = esp_reset_reason();
    const char* reset_reason_str = get_reset_reason_string(reset_reason);
    boot_count++;
    
    // Open log file in append mode
    log_file = fopen(LOG_FILE_PATH, "a");
    if (log_file == NULL) {
        ESP_LOGE(TAG, "Failed to open log file");
        return ESP_FAIL;
    }
    
    // Check file size and rotate if needed
    struct stat st;
    if (stat(LOG_FILE_PATH, &st) == 0) {
        if (st.st_size > LOG_MAX_SIZE) {
            fclose(log_file);
            
            // Rotate: delete old backup, rename current to backup
            remove(LOG_FILE_PATH ".old");
            rename(LOG_FILE_PATH, LOG_FILE_PATH ".old");
            
            // Open new file
            log_file = fopen(LOG_FILE_PATH, "a");
            if (log_file == NULL) {
                ESP_LOGE(TAG, "Failed to open new log file after rotation");
                return ESP_FAIL;
            }
            ESP_LOGI(TAG, "Rotated log file (was %ld KB)", st.st_size / 1024);
        }
    }
    
    // Write boot header
    char header[256];
    int len = snprintf(header, sizeof(header),
        "\n========== BOOT #%d ==========\n"
        "Reset reason: %s\n"
        "Free heap: %lu bytes\n"
        "===============================\n",
        (int)boot_count, reset_reason_str, esp_get_free_heap_size());
    
    if (len > 0) {
        fwrite(header, 1, len, log_file);
        fflush(log_file);
    }
    
    ESP_LOGI(TAG, "Log handler initialized (boot #%d, reason: %s)", 
             (int)boot_count, reset_reason_str);
    
    return ESP_OK;
}

void log_write(const char *format, ...)
{
    if (log_mutex == NULL || log_file == NULL) {
        return;
    }
    
    char temp_buffer[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(temp_buffer, sizeof(temp_buffer), format, args);
    va_end(args);
    
    if (len <= 0) {
        return;
    }
    
    // Ensure newline at the end
    if (temp_buffer[len - 1] != '\n') {
        if (len < sizeof(temp_buffer) - 1) {
            temp_buffer[len++] = '\n';
        }
    }
    
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        fwrite(temp_buffer, 1, len, log_file);
        fflush(log_file);
        xSemaphoreGive(log_mutex);
    }
}


size_t log_get_buffer(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0 || log_mutex == NULL || log_file == NULL) {
        return 0;
    }
    
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    
    // Flush any pending writes
    fflush(log_file);
    
    // Reopen file in read mode to properly read contents
    FILE *read_file = fopen(LOG_FILE_PATH, "r");
    if (read_file == NULL) {
        xSemaphoreGive(log_mutex);
        return 0;
    }
    
    // Get file size
    fseek(read_file, 0, SEEK_END);
    long file_size = ftell(read_file);
    fseek(read_file, 0, SEEK_SET);
    
    // Read file contents
    size_t read_size = (file_size < buffer_size - 1) ? file_size : buffer_size - 1;
    size_t bytes_read = fread(buffer, 1, read_size, read_file);
    buffer[bytes_read] = '\0';
    
    fclose(read_file);
    
    xSemaphoreGive(log_mutex);
    
    return bytes_read;
}

const char* log_get_buffer_ptr(void)
{
    return NULL;  // File-based logging doesn't expose raw pointer
}

size_t log_get_size(void)
{
    if (log_mutex == NULL || log_file == NULL) {
        return 0;
    }
    
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    
    // Flush to ensure file is up to date
    fflush(log_file);
    
    // Use stat to get file size reliably
    struct stat st;
    size_t file_size = 0;
    if (stat(LOG_FILE_PATH, &st) == 0) {
        file_size = st.st_size;
    }
    
    xSemaphoreGive(log_mutex);
    
    return file_size;
}

void log_clear(void)
{
    if (log_mutex == NULL || log_file == NULL) {
        return;
    }
    
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    
    fclose(log_file);
    remove(LOG_FILE_PATH);
    remove(LOG_FILE_PATH ".old");
    
    log_file = fopen(LOG_FILE_PATH, "a");
    boot_count = 0;
    
    xSemaphoreGive(log_mutex);
}

uint32_t log_get_boot_count(void)
{
    return boot_count;
}
