#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#define MAX_STRING_LEN 32
#define MAX_USERS 10
#define MAX_USERNAME_LEN 32
#define MAX_LINE_LEN 256

typedef struct {
    int port;
    char socket_type[MAX_STRING_LEN];
    char users[MAX_USERS][MAX_USERNAME_LEN];
    int user_count;
} Config;

Config parse_config(const char *filename);
int user_allowed(const Config *config, const char *username);
void print_config(const Config *config);

#endif
