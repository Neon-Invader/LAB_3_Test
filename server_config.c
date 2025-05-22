#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "server_config.h"

static void trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    *(end+1) = 0;
}

ServerConfig parse_server_config(const char *filename) {
    ServerConfig config = {
        .port = 8080,
        .socket_type = "stream",
        .max_connections = 10,
        .log_path = "/var/log/myrpc.log"
    };

    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Warning: Failed to open config file, using defaults");
        return config;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Удаление комментариев и пустых строк
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        if (strlen(line) == 0) continue;

        // Разделение ключа и значения
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "\n");
        if (!key || !value) continue;

        // Удаление пробелов
        trim_whitespace(key);
        trim_whitespace(value);

        if (strcmp(key, "port") == 0) {
            config.port = atoi(value);
        } 
        else if (strcmp(key, "socket_type") == 0) {
            strncpy(config.socket_type, value, sizeof(config.socket_type)-1);
        }
        else if (strcmp(key, "max_connections") == 0) {
            config.max_connections = atoi(value);
        }
        else if (strcmp(key, "log_path") == 0) {
            strncpy(config.log_path, value, sizeof(config.log_path)-1);
        }
    }

    fclose(file);
    return config;
}

int validate_server_config(const ServerConfig *config) {
    if (config->port < 1 || config->port > 65535) {
        fprintf(stderr, "Invalid port number: %d\n", config->port);
        return 0;
    }
    
    if (strcmp(config->socket_type, "stream") != 0 && 
        strcmp(config->socket_type, "dgram") != 0) {
        fprintf(stderr, "Invalid socket type: %s\n", config->socket_type);
        return 0;
    }
    
    if (config->max_connections < 1) {
        fprintf(stderr, "Invalid max connections: %d\n", config->max_connections);
        return 0;
    }
    
    return 1;
}
