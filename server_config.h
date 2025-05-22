#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

typedef struct {
    int port;
    char socket_type[16];  // "stream" или "dgram"
    int max_connections;
    char log_path[256];
} ServerConfig;

ServerConfig parse_server_config(const char *filename);
int validate_server_config(const ServerConfig *config);

#endif
