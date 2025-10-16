#include "tcp_connect.h"
#include "log_handler.h"
#include "usb_handler.h"
#include "esp_system.h"
#include <errno.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "TCP_CONNECT"

/* TODO: Make the variable "device_busy" false if the usb is unbound */
bool device_busy = false;
static int sock;
static SemaphoreHandle_t sock_mutex = NULL;
static submit recv_submit;
// static ssize_t size;
// static char rx_buffer[128];

// Thread-safe socket send function
int tcp_send_locked(int socket, const void *data, size_t length, int flags)
{
    int result = -1;
    if (sock_mutex != NULL && xSemaphoreTake(sock_mutex, portMAX_DELAY) == pdTRUE) {
        result = send(socket, data, length, flags);
        xSemaphoreGive(sock_mutex);
    } else {
        log_write("[TCP] ERROR: Failed to acquire socket mutex");
    }
    return result;
}

esp_err_t tcp_server_init(void)
{
    log_write("[TCP] Initializing NVS flash...");
    ESP_ERROR_CHECK(nvs_flash_init());
    
    log_write("[TCP] Initializing network interface...");
    ESP_ERROR_CHECK(esp_netif_init());
    
    log_write("[TCP] Creating event loop...");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    log_write("[TCP] Connecting to network...");
    ESP_ERROR_CHECK(example_connect());
    
    log_write("[TCP] Network initialization complete");
    return ESP_OK;
}

static void do_recv()
{
    log_write("[TCP] *** do_recv() task started ***");
    
    int len;
    usbip_header_common dev_recv;
    log_write("[TCP] Variables allocated, sock=%d", sock);
    log_write("[TCP] Starting receive task, waiting for USB/IP commands...");
    
    while (1)
    {
        if (!device_busy)
        {
            // usbip_header_common dev_recv;
            log_write("[TCP] Waiting for USB/IP command (device not busy)...");
            len = recv(sock, &dev_recv, sizeof(usbip_header_common), 0); // Blocking call
            if (len < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Timeout occurred - this is normal, just continue waiting
                    log_write("[TCP] Socket receive timeout, continuing to wait for data...");
                    continue;
                }
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                log_write("[TCP] ERROR: recv failed, errno %d (%s)", errno, strerror(errno));
                break;
            }
            else if (len == 0)
            {
                ESP_LOGW(TAG, "Connection closed");
                log_write("[TCP] Connection closed by client (possibly after device list query)");
                // Don't reboot - this is normal when client just queries device list
                // Clean up and wait for new connection
                break; // Exit recv loop, close socket, wait for new connection
            }
            else if (len > 0)
            {
                log_write("[TCP] Received %d bytes, version=0x%04x, command=0x%04x", 
                         len, ntohs(dev_recv.usbip_version), ntohs(dev_recv.command_code));
                
                if (ntohs(dev_recv.usbip_version) == USBIP_VERSION)
                {
                    log_write("[TCP] Valid USB/IP command received: 0x%04x", ntohs(dev_recv.command_code));
                    
                    if (loop_handle == NULL) {
                        log_write("[TCP] ERROR: Event loop not initialized!");
                        ESP_LOGE(TAG, "Event loop handle is NULL!");
                        break;
                    }
                    
                    tcp_data buffer;
                    buffer.sock = sock;
                    buffer.len = sizeof(usbip_header_common);
                    buffer.rx_buffer = (uint8_t *)&dev_recv;
                    
                    log_write("[TCP] Posting event to handler, command=0x%04x", ntohs(dev_recv.command_code));
                    esp_err_t err = esp_event_post_to(loop_handle, USBIP_EVENT_BASE, ntohs(dev_recv.command_code), 
                                                       (void *)&buffer, sizeof(tcp_data), portMAX_DELAY);
                    if (err != ESP_OK) {
                        log_write("[TCP] ERROR: Failed to post event: %s", esp_err_to_name(err));
                    } else {
                        log_write("[TCP] Event posted successfully");
                    }
                    
                    // For OP_REQ_IMPORT (0x8003), wait for device_busy to be set before continuing
                    // This ensures the import response is fully sent before we start waiting for URBs
                    if (ntohs(dev_recv.command_code) == 0x8003) {
                        log_write("[TCP] Waiting for import to complete and device_busy to be set...");
                        int wait_count = 0;
                        while (!device_busy && wait_count < 100) {  // Wait up to 1 second
                            vTaskDelay(pdMS_TO_TICKS(10));
                            wait_count++;
                        }
                        if (device_busy) {
                            log_write("[TCP] Import complete, device_busy set after %d ms", wait_count * 10);
                        } else {
                            log_write("[TCP] WARNING: device_busy not set after 1 second wait!");
                        }
                    }
                }
                else
                {
                    log_write("[TCP] ERROR: Invalid USB/IP version: 0x%04x (expected 0x%04x)", 
                             ntohs(dev_recv.usbip_version), USBIP_VERSION);
                }
            }
        }
        if (device_busy)
        {
            log_write("[TCP] Device busy mode - handling URB commands");
            log_write("[TCP] Socket descriptor: %d", sock);
            
            /* Check socket state */
            int error = 0;
            socklen_t errlen = sizeof(error);
            int ret = getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &errlen);
            log_write("[TCP] Socket state: getsockopt(SO_ERROR) returned %d, error=%d (%s)", 
                     ret, error, error ? strerror(error) : "no error");
            
            /* Check if there's any pending data on the socket */
            int pending = 0;
            ret = ioctl(sock, FIONREAD, &pending);
            if (ret < 0) {
                log_write("[TCP] WARNING: ioctl(FIONREAD) failed, errno=%d (%s)", errno, strerror(errno));
            } else {
                log_write("[TCP] Pending bytes in socket buffer: %d", pending);
            }
            
            /* TODO : Start dealing with URB command codes */
            usbip_header_basic header;
            len = 0;
            while (1)
            {
                log_write("[TCP] Waiting for URB header (%d bytes)...", sizeof(usbip_header_basic));
                len = recv(sock, &header, sizeof(usbip_header_basic), 0);

                if (len < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // Timeout occurred - this is normal, just continue waiting
                        log_write("[TCP] URB socket receive timeout, continuing to wait...");
                        continue;
                    }
                    log_write("[TCP] ERROR: recv failed in URB loop, errno %d (%s)", errno, strerror(errno));
                    break;
                } else if (len == 0) {
                    log_write("[TCP] Connection closed in URB loop - client detached");
                    log_write("[TCP] Cleaning up and resetting device_busy flag...");
                    device_busy = false;
                    break; // Exit URB loop, close socket, wait for new connection
                } else if (len > 0)
                {
                    uint32_t cmd = ntohl(header.command);
                    log_write("[TCP] Received URB command: 0x%08x, seqnum=%u", cmd, ntohl(header.seqnum));
                    
                    switch (cmd)
                    {
                    case USBIP_CMD_SUBMIT:
                    {
                        log_write("[TCP] USBIP_CMD_SUBMIT received, reading command data...");
                        usbip_cmd_submit cmd_submit;
                        
                        // Read cmd_submit header (without transfer_buffer)
                        int cmd_header_size = sizeof(usbip_cmd_submit) - 1024;
                        int bytes_read = recv(sock, &cmd_submit, cmd_header_size, 0);
                        if (bytes_read != cmd_header_size) {
                            log_write("[TCP] ERROR: Failed to read cmd_submit header, got %d bytes, expected %d", 
                                     bytes_read, cmd_header_size);
                            break;
                        }
                        log_write("[TCP] Read cmd_submit header (%d bytes)", bytes_read);
                        
                        uint32_t transfer_len = ntohl(cmd_submit.transfer_buffer_length);
                        log_write("[TCP] Transfer length=%u, direction=%u", transfer_len, ntohl(header.direction));
                        
                        // For host-to-device (OUT), read the transfer data
                        if (ntohl(header.direction) == 0 && transfer_len > 0) {
                            if (transfer_len > 1024) {
                                log_write("[TCP] ERROR: Transfer length %u exceeds buffer size 1024", transfer_len);
                                break;
                            }
                            log_write("[TCP] Reading %u bytes of transfer data...", transfer_len);
                            bytes_read = recv(sock, &cmd_submit.transfer_buffer[0], transfer_len, 0);
                            if (bytes_read != transfer_len) {
                                log_write("[TCP] ERROR: Failed to read transfer data, got %d bytes, expected %u", 
                                         bytes_read, transfer_len);
                                break;
                            }
                            log_write("[TCP] Transfer data read successfully (%d bytes)", bytes_read);
                        }
                        
                        // Populate submit structure
                        recv_submit.header = header;
                        recv_submit.cmd_submit = cmd_submit;
                        recv_submit.sock = sock;
                        
                        log_write("[TCP] Posting SUBMIT event to USB handler...");
                        esp_err_t err = esp_event_post_to(loop_handle2, USBIP_EVENT_BASE, USBIP_CMD_SUBMIT, 
                                                          (void *)&recv_submit, sizeof(submit), portMAX_DELAY);
                        if (err != ESP_OK) {
                            log_write("[TCP] ERROR: Failed to post SUBMIT event: %s", esp_err_to_name(err));
                        } else {
                            log_write("[TCP] SUBMIT event posted successfully");
                        }
                        break;
                    }

                    case USBIP_CMD_UNLINK:
                    {
                        log_write("[TCP] USBIP_CMD_UNLINK received");
                        usbip_cmd_unlink cmd_unlink;
                        len = recv(sock, &cmd_unlink, sizeof(usbip_cmd_unlink), 0);
                        log_write("[TCP] Unlink request for seqnum=%u", ntohl(cmd_unlink.unlink_seqnum));
                        init_unlink(ntohl(cmd_unlink.unlink_seqnum));

                        /* TODO: REPLY with RET_UNLINK after error check*/
                        usbip_ret_unlink ret_unlink;
                        ret_unlink.base.command = htonl(USBIP_RET_UNLINK);
                        ret_unlink.base.seqnum = htonl(0x00000002);
                        ret_unlink.base.devid = htonl(0x00000000);
                        ret_unlink.base.direction = htonl(0x00000000);
                        ret_unlink.base.ep = htonl(0x00000000);

                        ret_unlink.status = htonl(0);

                        memset(ret_unlink.padding, 0, 24);
                        len = tcp_send_locked(sock, &ret_unlink, sizeof(usbip_ret_unlink), 0);
                        log_write("[TCP] Sent USBIP_RET_UNLINK response: %d bytes", len);
                        ESP_LOGI(TAG, "Submitted ret_unlink %u", len);
                        len = 0;
                        break;
                    }
                    default:
                        log_write("[TCP] WARNING: Unknown URB command: 0x%08x", cmd);
                        printf("SUCCESS\n");
                        break;
                    }
                }
            }
        }

        /* Continue looping - don't break here! 
         * The loop should continue until a socket error or connection close */
    }
    
    log_write("[TCP] Receive loop ended, cleaning up connection");
    
    // Mark device as not busy so no more transfers are accepted
    device_busy = false;
    
    // Reset pending transfer flags (declared in usb_handler.h)
    ep1_transfer_pending = false;
    ep2_transfer_pending = false;
    
    close(sock);
    sock = -1;  // Invalidate socket
    
    log_write("[TCP] Socket closed, ready for new connection");
    // Return to tcp_server_start() which will accept new connections
}

void tcp_server_start(void *pvParameters)
{
    ESP_LOGI(TAG, "TCP server task started");
    
    // Create mutex for socket operations
    sock_mutex = xSemaphoreCreateMutex();
    if (sock_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create socket mutex");
        log_write("[TCP] ERROR: Failed to create socket mutex");
        vTaskDelete(NULL);
        return;
    }
    log_write("[TCP] Socket mutex created successfully");
    
    int addr_family = AF_INET;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT);
    ip_protocol = IPPROTO_IP;

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        log_write("[TCP] ERROR: Failed to create socket, errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");
    log_write("[TCP] Socket created successfully");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        log_write("[TCP] ERROR: Failed to bind to port %d, errno %d", PORT, errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);
    log_write("[TCP] Socket bound to port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        log_write("[TCP] ERROR: Failed to listen on port %d, errno %d", PORT, errno);
        goto CLEAN_UP;
    }
    log_write("[TCP] TCP server listening on port %d for USB/IP connections", PORT);
    
    while (1)
    {
        ESP_LOGI(TAG, "Socket listening");
        log_write("[TCP] Waiting for client connection...");
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 second delay between log messages

        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        
        ESP_LOGI(TAG, "About to call accept()...");
        sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        
        ESP_LOGI(TAG, "Accept returned: sock=%d, errno=%d", sock, errno);
        
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Accept failed: errno %d", errno);
            log_write("[TCP] ERROR: accept() failed, errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Successfully accepted connection
        ESP_LOGI(TAG, "Connection accepted, sock=%d", sock);
        log_write("[TCP] Client connected successfully");
        
        // Get client address
        char client_ip[32] = "unknown";
        if (source_addr.ss_family == PF_INET)
        {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)&source_addr;
            inet_ntop(AF_INET, &addr_in->sin_addr, client_ip, sizeof(client_ip));
        }
        
        log_write("[TCP] Client IP: %s", client_ip);
        
        log_write("[TCP] Setting socket keepalive options...");
        // Set tcp keepalive options
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        log_write("[TCP] SO_KEEPALIVE set");
        
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        log_write("[TCP] TCP_KEEPIDLE set");
        
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        log_write("[TCP] TCP_KEEPINTVL set");
        
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        log_write("[TCP] TCP_KEEPCNT set");
        
        // Set socket receive timeout to 30 seconds to prevent premature connection closure
        struct timeval timeout;
        timeout.tv_sec = 30;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        log_write("[TCP] SO_RCVTIMEO set to 30 seconds");
        
        // Disable Nagle's algorithm for lower latency
        int nodelay = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(int));
        log_write("[TCP] TCP_NODELAY set");

        log_write("[TCP] Handling client connection directly (not creating separate task)...");
        
        // Handle the connection directly instead of creating a task
        do_recv();
        
        // After connection closes, clean up and wait for next connection
        log_write("[TCP] Client disconnected, ready for new connection");
    }
CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}