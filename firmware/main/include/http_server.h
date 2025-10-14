#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include "sdkconfig.h"

#ifdef CONFIG_ENABLE_HTTP_SERVER

#include <esp_http_server.h>
#include "esp_log.h"

/**
 * @brief Initialize and start the HTTP server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t http_server_init(void);

/**
 * @brief Stop the HTTP server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t http_server_stop(void);

#else

#include "esp_err.h"

// Stub implementations when HTTP server is disabled
static inline esp_err_t http_server_init(void) { return ESP_OK; }
static inline esp_err_t http_server_stop(void) { return ESP_OK; }

#endif // CONFIG_ENABLE_HTTP_SERVER

#endif // __HTTP_SERVER_H__
