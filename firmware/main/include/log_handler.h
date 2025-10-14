#ifndef __LOG_HANDLER_H__
#define __LOG_HANDLER_H__

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define LOG_BUFFER_SIZE (4 * 1024)  // 4KB circular buffer in RTC memory (survives reboots)

#ifdef CONFIG_ENABLE_LOG_HANDLER

/**
 * @brief Initialize the in-memory log handler
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t log_handler_init(void);

/**
 * @brief Write a log entry to the in-memory buffer
 * 
 * @param format printf-style format string
 * @param ... variable arguments
 */
void log_write(const char *format, ...);

/**
 * @brief Get the current log buffer contents
 * 
 * @param buffer Pointer to buffer to copy log data into
 * @param buffer_size Size of the destination buffer
 * @return size_t Number of bytes written to buffer
 */
size_t log_get_buffer(char *buffer, size_t buffer_size);

/**
 * @brief Get the current log buffer pointer (for direct access)
 * 
 * @return const char* Pointer to the log buffer
 */
const char* log_get_buffer_ptr(void);

/**
 * @brief Get the current size of logged data
 * 
 * @return size_t Size of logged data
 */
size_t log_get_size(void);

/**
 * @brief Clear the log buffer
 */
void log_clear(void);

/**
 * @brief Get the current boot count
 * 
 * @return uint32_t Number of reboots since power-on
 */
uint32_t log_get_boot_count(void);

#else

// Stub implementations when log handler is disabled
static inline esp_err_t log_handler_init(void) { return ESP_OK; }
static inline void log_write(const char *format, ...) { (void)format; }
static inline size_t log_get_buffer(char *buffer, size_t buffer_size) { (void)buffer; (void)buffer_size; return 0; }
static inline const char* log_get_buffer_ptr(void) { return NULL; }
static inline size_t log_get_size(void) { return 0; }
static inline void log_clear(void) { }
static inline uint32_t log_get_boot_count(void) { return 0; }

#endif // CONFIG_ENABLE_LOG_HANDLER

#endif // __LOG_HANDLER_H__
