# List of source files - always included
set(SRCS "src/main.c"
         "src/usbip_server.c"
         "src/usb_handler.c"
         "src/tcp_connect.c")

# Conditionally add log handler
if(CONFIG_ENABLE_LOG_HANDLER)
    list(APPEND SRCS "src/log_handler.c")
endif()

# Conditionally add HTTP server
if(CONFIG_ENABLE_HTTP_SERVER)
    list(APPEND SRCS "src/http_server.c")
endif()

# List of include directories
set(INCLUDE_DIRS "include")

# Build list of private required components
set(PRIV_REQUIRED_COMPONENTS esp_wifi lwip nvs_flash esp_event protocol_examples_common)

# Always include esp_http_server in PRIV_REQUIRES to avoid dependency issues
# The actual compilation of http_server.c is still controlled by CONFIG_ENABLE_HTTP_SERVER
list(APPEND PRIV_REQUIRED_COMPONENTS esp_http_server)

# Register the component and list all its private dependencies
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS ${INCLUDE_DIRS}
                       REQUIRES usb
                       PRIV_REQUIRES ${PRIV_REQUIRED_COMPONENTS})
