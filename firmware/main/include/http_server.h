#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include "esp_err.h"

// Forward declare to avoid including esp_http_server.h in header
#ifdef CONFIG_ENABLE_HTTP_SERVER

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

// Stub implementations when HTTP server is disabled
static inline esp_err_t http_server_init(void) { return ESP_OK; }
static inline esp_err_t http_server_stop(void) { return ESP_OK; }

#endif // CONFIG_ENABLE_HTTP_SERVER

#endif // __HTTP_SERVER_H__
