#include "http_server.h"
#include "log_handler.h"
#include <string.h>

static const char *TAG = "HTTP_SERVER";
static httpd_handle_t server = NULL;

/* HTTP GET handler for /logs endpoint */
static esp_err_t logs_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    
    size_t log_size = log_get_size();
    const char *log_ptr = log_get_buffer_ptr();
    
    if (log_size == 0 || log_ptr == NULL) {
        const char *empty = "(No logs)\n";
        return httpd_resp_send(req, empty, strlen(empty));
    }
    
    return httpd_resp_send(req, log_ptr, log_size);
}

/* HTTP GET handler for root endpoint */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    // Simple redirect to logs
    const char *html = 
        "<html><head><meta http-equiv='refresh' content='0;url=/logs'></head>"
        "<body>Redirecting to logs...</body></html>";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    
    return ESP_OK;
}

/* HTTP GET handler for /clear endpoint */
static esp_err_t clear_logs_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Clearing logs");
    log_clear();
    
    const char *resp = "Logs cleared successfully\n";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, resp, strlen(resp));
    
    return ESP_OK;
}

/* URI handlers */
static const httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t logs_uri = {
    .uri       = "/logs",
    .method    = HTTP_GET,
    .handler   = logs_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t clear_uri = {
    .uri       = "/clear",
    .method    = HTTP_GET,
    .handler   = clear_logs_handler,
    .user_ctx  = NULL
};

esp_err_t http_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    
    // Increase limits to handle larger requests
    config.max_uri_handlers = 8;
    config.max_resp_headers = 8;
    config.backlog_conn = 5;
    config.stack_size = 8192;
    
    // Increase recv and send timeouts
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &logs_uri);
        httpd_register_uri_handler(server, &clear_uri);
        
        ESP_LOGI(TAG, "HTTP server started successfully");
        log_write("[HTTP] HTTP server started on port 8080");
        
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
}

esp_err_t http_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
        return ESP_OK;
    }
    return ESP_FAIL;
}
