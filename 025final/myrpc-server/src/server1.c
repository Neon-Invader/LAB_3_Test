#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "config_parser.h"
#include "libmysyslog.h"

#define BUFFER_SIZE 1024
#define MAIN_CONFIG "/etc/myRPC/myRPC.conf"
#define USERS_CONFIG "/etc/myRPC/users.conf"
#define LOG_FILE "/var/log/myrpc.log"

volatile sig_atomic_t stop;

void handle_signal(int sig) {
    stop = 1;
}

int user_allowed(const char *username) {
    Config config = parse_config(USERS_CONFIG);
    
    // Проверка основного пользователя
    if (strlen(config.user) > 0 && strcmp(config.user, username) == 0)
        return 1;
    
    // Проверка дополнительных пользователей
    for (size_t i = 0; i < config.users_count; i++) {
        if (strcmp(config.users[i], username) == 0)
            return 1;
    }
    
    return 0;
}

void execute_command(const char *command, char *stdout_path, char *stderr_path) {
    int stdout_fd = mkstemp(stdout_path);
    int stderr_fd = mkstemp(stderr_path);
    
    if (stdout_fd == -1 || stderr_fd == -1) {
        mysyslog("Temp file error", ERROR, 0, 0, LOG_FILE);
        return;
    }
    
    close(stdout_fd);
    close(stderr_fd);

    char cmd[BUFFER_SIZE];
    snprintf(cmd, BUFFER_SIZE, "%s >%s 2>%s", command, stdout_path, stderr_path);
    
    if (system(cmd) == -1) {
        mysyslog("Command failed", ERROR, 0, 0, LOG_FILE);
    }
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    Config config = parse_config(MAIN_CONFIG);
    if (config.port <= 0) {
        fprintf(stderr, "Invalid port: %d\n", config.port);
        return 1;
    }

    int sockfd = socket(AF_INET, 
                      strcmp(config.socket_type, "stream") == 0 ? SOCK_STREAM : SOCK_DGRAM, 
                      0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(config.port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    if (strcmp(config.socket_type, "stream") == 0) {
        listen(sockfd, 5);
        mysyslog("TCP server started", INFO, 0, 0, LOG_FILE);
    } else {
        mysyslog("UDP server started", INFO, 0, 0, LOG_FILE);
    }

    while (!stop) {
        struct sockaddr_in cli_addr;
        socklen_t addr_len = sizeof(cli_addr);
        char buffer[BUFFER_SIZE];
        
        int connfd = strcmp(config.socket_type, "stream") == 0 ? 
            accept(sockfd, (struct sockaddr*)&cli_addr, &addr_len) : sockfd;

        ssize_t n = recvfrom(connfd, buffer, BUFFER_SIZE, 0,
                           (struct sockaddr*)&cli_addr, &addr_len);
        if (n <= 0) continue;

        buffer[n] = '\0';
        
        char *username = strtok(buffer, ":");
        char *command = strtok(NULL, "");
        if (!username || !command) {
            mysyslog("Invalid request", WARN, 0, 0, LOG_FILE);
            continue;
        }

        char response[BUFFER_SIZE] = {0};
        if (user_allowed(username)) {
            char stdout_path[32], stderr_path[32];
            execute_command(command, stdout_path, stderr_path);
            
            FILE *fp = fopen(stdout_path, "r");
            if (fp) {
                fread(response, 1, BUFFER_SIZE-1, fp);
                fclose(fp);
            } else {
                strcpy(response, "Execution error");
            }
            remove(stdout_path);
            remove(stderr_path);
        } else {
            snprintf(response, BUFFER_SIZE, "Access denied for %s", username);
        }

        sendto(connfd, response, strlen(response), 0,
             (struct sockaddr*)&cli_addr, addr_len);
        
        if (connfd != sockfd)
            close(connfd);
    }

    close(sockfd);
    mysyslog("Server stopped", INFO, 0, 0, LOG_FILE);
    return 0;
}
