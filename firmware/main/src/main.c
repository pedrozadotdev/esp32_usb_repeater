#include "usbip_server.h"
#include "log_handler.h"
#include "http_server.h"
#include "tcp_connect.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include <inttypes.h>

int app_main(void)
{
    // Initialize the log handler first
    log_handler_init();
    
    uint32_t boot = log_get_boot_count();
    log_write("[MAIN] ========================================");
    log_write("[MAIN] ESP32 USB Repeater Starting (Boot #%" PRIu32 ")", boot);
    log_write("[MAIN] ========================================");
    
    // Initialize network/TCP stack
    log_write("[MAIN] Initializing network...");
    tcp_server_init();
    
    // Initialize USB/IP server BEFORE starting TCP server (must create event loop first!)
    log_write("[MAIN] Initializing USB/IP server...");
    usbip_server_init();
    
    // Start TCP server on port 3240 for USB/IP (large stack for network operations)
    log_write("[MAIN] Free heap before TCP server: %d bytes", esp_get_free_heap_size());
    log_write("[MAIN] Starting TCP server on port 3240...");
    TaskHandle_t tcp_task_handle;
    BaseType_t ret = xTaskCreate(tcp_server_start, "tcp_server", 12288, NULL, 5, &tcp_task_handle);
    if (ret != pdPASS) {
        log_write("[MAIN] ERROR: Failed to create TCP server task!");
    } else {
        log_write("[MAIN] TCP server task created successfully");
    }
    
#ifdef CONFIG_ENABLE_HTTP_SERVER
    // Start HTTP server for log access
    log_write("[MAIN] Starting HTTP server on port 8080...");
    http_server_init();
#endif
    
    log_write("[MAIN] All systems initialized successfully");
    log_write("[MAIN] USB/IP server listening on port 3240");
#ifdef CONFIG_ENABLE_HTTP_SERVER
    log_write("[MAIN] HTTP log server listening on port 8080");
#endif
    
    return 0;
}