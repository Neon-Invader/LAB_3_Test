#ifndef CLIENT_CONFIG_H
#define CLIENT_CONFIG_H

typedef struct {
    char server_ip[16];
    int port;
    char socket_type[16];
    int timeout_ms;
} ClientConfig;

ClientConfig parse_client_config(const char *filename);
int validate_client_config(const ClientConfig *config);

#endif
