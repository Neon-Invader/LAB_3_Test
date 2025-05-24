#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "config_parser.h"

static void trim_whitespace(char *str) {
    if (str == NULL) {
        return;
    }

    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';

    if (start != str) {
        memmove(str, start, end - start + 2);
    }
}

static void add_user(Config *config, const char *username) {
    if (config == NULL || username == NULL) {
        return;
    }

    if (config->user_count >= MAX_USERS) {
        fprintf(stderr, "Maximum user count reached\n");
        return;
    }

    strncpy(config->users[config->user_count], username, MAX_USERNAME_LEN - 1);
    config->users[config->user_count][MAX_USERNAME_LEN - 1] = '\0';
    config->user_count++;
}

Config parse_config(const char *filename) {
    Config config;
    config.port = 0;
    strcpy(config.socket_type, "stream");
    config.user_count = 0;

    if (filename == NULL) {
        fprintf(stderr, "Error: NULL filename provided\n");
        return config;
    }

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Failed to open config file");
        return config;
    }

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        trim_whitespace(line);

        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        char *key = strtok(line, "=");
        char *value = strtok(NULL, "");

        if (key == NULL || value == NULL) {
            add_user(&config, line);
            continue;
        }

        trim_whitespace(key);
        trim_whitespace(value);

        if (strcmp(key, "port") == 0) {
            config.port = atoi(value);
        } 
        else if (strcmp(key, "socket_type") == 0) {
            strncpy(config.socket_type, value, MAX_STRING_LEN - 1);
            config.socket_type[MAX_STRING_LEN - 1] = '\0';
        }
        else if (strcmp(key, "user") == 0 || strcmp(key, "users") == 0) {
            char *user = strtok(value, ",");
            while (user != NULL) {
                trim_whitespace(user);
                add_user(&config, user);
                user = strtok(NULL, ",");
            }
        }
        else {
            add_user(&config, key);
        }
    }

    if (fclose(file) != 0) {
        perror("Warning: Failed to close config file");
    }

    return config;
}

int user_allowed(const Config *config, const char *username) {
    if (config == NULL || username == NULL) {
        return 0;
    }

    for (int i = 0; i < config->user_count; i++) {
        if (strcmp(config->users[i], username) == 0) {
            return 1;
        }
    }

    return 0;
}

void print_config(const Config *config) {
    if (config == NULL) {
        printf("Config is NULL\n");
        return;
    }

    printf("Port: %d\n", config->port);
    printf("Socket Type: %s\n", config->socket_type);
    printf("Users (%d):\n", config->user_count);
    for (int i = 0; i < config->user_count; i++) {
        printf("  - %s\n", config->users[i]);
    }
}
