#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include "client_config.h"

static void trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    *(end+1) = 0;
}

ClientConfig parse_client_config(const char *filename) {
    ClientConfig config = {
        .server_ip = "127.0.0.1",
        .port = 8080,
        .socket_type = "stream",
        .timeout_ms = 5000
    };

    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Warning: Failed to open config file, using defaults");
        return config;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        if (strlen(line) == 0) continue;

        char *key = strtok(line, "=");
        char *value = strtok(NULL, "\n");
        if (!key || !value) continue;

        trim_whitespace(key);
        trim_whitespace(value);

        if (strcmp(key, "server_ip") == 0) {
            strncpy(config.server_ip, value, sizeof(config.server_ip)-1);
        } 
        else if (strcmp(key, "port") == 0) {
            config.port = atoi(value);
        }
        else if (strcmp(key, "socket_type") == 0) {
            strncpy(config.socket_type, value, sizeof(config.socket_type)-1);
        }
        else if (strcmp(key, "timeout_ms") == 0) {
            config.timeout_ms = atoi(value);
        }
    }

    fclose(file);
    return config;
}

int validate_client_config(const ClientConfig *config) {
    struct in_addr addr;
    if (inet_pton(AF_INET, config->server_ip, &addr) != 1) {
        fprintf(stderr, "Invalid server IP: %s\n", config->server_ip);
        return 0;
    }
    
    if (config->port < 1 || config->port > 65535) {
        fprintf(stderr, "Invalid port number: %d\n", config->port);
        return 0;
    }
    
    if (strcmp(config->socket_type, "stream") != 0 && 
        strcmp(config->socket_type, "dgram") != 0) {
        fprintf(stderr, "Invalid socket type: %s\n", config->socket_type);
        return 0;
    }
    
    if (config->timeout_ms < 0) {
        fprintf(stderr, "Invalid timeout value: %d\n", config->timeout_ms);
        return 0;
    }
    
    return 1;
}
