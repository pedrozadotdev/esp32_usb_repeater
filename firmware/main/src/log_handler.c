#include "log_handler.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "soc/rtc.h"
#include <inttypes.h>

// Use RTC memory to survive reboots (but not power cycles)
static RTC_NOINIT_ATTR char log_buffer[LOG_BUFFER_SIZE];
static RTC_NOINIT_ATTR size_t log_write_pos;
static RTC_NOINIT_ATTR size_t log_size;
static RTC_NOINIT_ATTR uint32_t log_magic;
static RTC_NOINIT_ATTR uint32_t boot_count;

#define LOG_MAGIC 0xDEADBEEF

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
    
    // Get reset reason
    esp_reset_reason_t reset_reason = esp_reset_reason();
    const char* reset_reason_str = get_reset_reason_string(reset_reason);
    
    // Check if this is a reboot and logs are valid
    bool logs_preserved = (log_magic == LOG_MAGIC);
    
    if (logs_preserved) {
        // Logs survived reboot!
        boot_count++;
        ESP_LOGW(TAG, "=== REBOOT DETECTED (Boot #%" PRIu32 ") - Reason: %s ===", boot_count, reset_reason_str);
        ESP_LOGW(TAG, "Previous logs preserved in RTC memory (%zu bytes)", log_size);
        
        // Add reboot marker to logs
        char reboot_msg[256];
        snprintf(reboot_msg, sizeof(reboot_msg), 
                "\n\n========== REBOOT #%" PRIu32 " ==========\n"
                "Reset Reason: %s\n"
                "Time: Boot #%" PRIu32 "\n"
                "==============================\n\n", 
                boot_count, reset_reason_str, boot_count);
        
        // Append reboot marker
        size_t marker_len = strlen(reboot_msg);
        if (log_write_pos + marker_len < LOG_BUFFER_SIZE) {
            memcpy(log_buffer + log_write_pos, reboot_msg, marker_len);
            log_write_pos += marker_len;
            log_size = log_write_pos;
        } else {
            // Buffer full, make room
            size_t shift_amount = marker_len;
            memmove(log_buffer, log_buffer + shift_amount, LOG_BUFFER_SIZE - shift_amount);
            log_write_pos = LOG_BUFFER_SIZE - shift_amount;
            memcpy(log_buffer + log_write_pos, reboot_msg, marker_len);
            log_write_pos += marker_len;
            log_size = log_write_pos;
        }
    } else {
        // First boot or power cycle - initialize fresh
        memset(log_buffer, 0, LOG_BUFFER_SIZE);
        log_write_pos = 0;
        log_size = 0;
        boot_count = 1;
        log_magic = LOG_MAGIC;
        ESP_LOGI(TAG, "Log handler initialized with %d KB buffer (fresh start)", LOG_BUFFER_SIZE / 1024);
        ESP_LOGI(TAG, "Reset reason: %s", reset_reason_str);
    }
    
    return ESP_OK;
}

void log_write(const char *format, ...)
{
    if (log_mutex == NULL) {
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
    
    // Ensure we have a newline at the end
    if (temp_buffer[len - 1] != '\n') {
        if (len < sizeof(temp_buffer) - 1) {
            temp_buffer[len++] = '\n';
            temp_buffer[len] = '\0';
        }
    }
    
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    
    // Calculate how much space we need
    size_t needed = len;
    
    // If buffer would overflow, shift content (circular buffer behavior)
    if (log_write_pos + needed > LOG_BUFFER_SIZE) {
        // Move remaining content to the beginning
        size_t keep_size = LOG_BUFFER_SIZE - needed;
        if (keep_size > log_size) {
            keep_size = log_size;
        }
        
        if (keep_size > 0) {
            memmove(log_buffer, log_buffer + log_size - keep_size, keep_size);
        }
        log_write_pos = keep_size;
        log_size = keep_size;
    }
    
    // Copy the new log entry
    memcpy(log_buffer + log_write_pos, temp_buffer, len);
    log_write_pos += len;
    log_size = log_write_pos;
    
    // Ensure null termination
    if (log_size < LOG_BUFFER_SIZE) {
        log_buffer[log_size] = '\0';
    }
    
    xSemaphoreGive(log_mutex);
    
    // Also print to console
    printf("%s", temp_buffer);
}

size_t log_get_buffer(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0 || log_mutex == NULL) {
        return 0;
    }
    
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    
    size_t copy_size = log_size;
    if (copy_size >= buffer_size) {
        copy_size = buffer_size - 1;
    }
    
    memcpy(buffer, log_buffer, copy_size);
    buffer[copy_size] = '\0';
    
    xSemaphoreGive(log_mutex);
    
    return copy_size;
}

const char* log_get_buffer_ptr(void)
{
    return log_buffer;
}

size_t log_get_size(void)
{
    if (log_mutex == NULL) {
        return 0;
    }
    
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    size_t size = log_size;
    xSemaphoreGive(log_mutex);
    
    return size;
}

void log_clear(void)
{
    if (log_mutex == NULL) {
        return;
    }
    
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    memset(log_buffer, 0, LOG_BUFFER_SIZE);
    log_write_pos = 0;
    log_size = 0;
    boot_count = 1;
    xSemaphoreGive(log_mutex);
}

uint32_t log_get_boot_count(void)
{
    return boot_count;
}
