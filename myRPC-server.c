#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/wait.h>
#include <libmysyslog.h>

#define CONFIG_FILE "/etc/myRPC/myRPC.conf"
#define USERS_FILE "/etc/myRPC/users.conf"
#define BUFFER_SIZE 4096
#define TMP_FILE_TEMPLATE "/tmp/myRPC_XXXXXX"

typedef struct {
    int port;
    int socket_type;
} ServerConfig;

int parse_config(ServerConfig *config) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        log_error("Failed to open config file");
        return -1;
    }
    
    char line[100];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        
        if (strstr(line, "port =")) {
            config->port = atoi(strchr(line, '=') + 2);
        }
        else if (strstr(line, "socket_type = stream")) {
            config->socket_type = SOCK_STREAM;
        }
        else if (strstr(line, "socket_type = dgram")) {
            config->socket_type = SOCK_DGRAM;
        }
    }
    
    fclose(fp);
    return 0;
}

int is_user_allowed(const char *username) {
    FILE *fp = fopen(USERS_FILE, "r");
    if (!fp) {
        log_error("Failed to open users file");
        return 0;
    }
    
    char line[100];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '#') continue;
        
        if (strcmp(line, username) == 0) {
            fclose(fp);
            return 1;
        }
    }
    
    fclose(fp);
    return 0;
}

FILE* create_temp_file(char *template) {
    char *name = mktemp(template);
    if (!name) return NULL;
    return fopen(name, "w+");
}

void execute_command(const char *cmd, int client_sock) {
    char stdout_path[50], stderr_path[50];
    strcpy(stdout_path, TMP_FILE_TEMPLATE);
    strcpy(stderr_path, TMP_FILE_TEMPLATE);
    
    FILE *stdout_file = create_temp_file(stdout_path);
    FILE *stderr_file = create_temp_file(stderr_path);
    
    if (!stdout_file || !stderr_file) {
        log_error("Failed to create temp files");
        send(client_sock, "1:\"Temp file error\"", 18, 0);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Перенаправляем вывод
        dup2(fileno(stdout_file), STDOUT_FILENO);
        dup2(fileno(stderr_file), STDERR_FILENO);
        
        fclose(stdout_file);
        fclose(stderr_file);
        
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        
        // Читаем результат
        rewind(stdout_file);
        rewind(stderr_file);
        
        char stdout_buf[BUFFER_SIZE] = {0};
        char stderr_buf[BUFFER_SIZE] = {0};
        
        fread(stdout_buf, 1, BUFFER_SIZE-1, stdout_file);
        fread(stderr_buf, 1, BUFFER_SIZE-1, stderr_file);
        
        // Формируем ответ
        char response[BUFFER_SIZE*2];
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            snprintf(response, sizeof(response), "0:\"%s\"", stdout_buf);
        } else {
            snprintf(response, sizeof(response), "1:\"%s\"", stderr_buf);
        }
        
        send(client_sock, response, strlen(response), 0);
        
        // Закрываем и удаляем временные файлы
        fclose(stdout_file);
        fclose(stderr_file);
        remove(stdout_path);
        remove(stderr_path);
    } else {
        log_error("Fork failed");
        send(client_sock, "1:\"Command execution failed\"", 28, 0);
    }
}

int main() {
    ServerConfig config = {1234, SOCK_STREAM};
    if (parse_config(&config) != 0) {
        log_error("Using default configuration");
    }

    int sockfd = socket(AF_INET, config.socket_type, 0);
    if (sockfd < 0) {
        log_error("Socket creation failed");
        return 1;
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(config.port);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        log_error("Bind failed");
        close(sockfd);
        return 1;
    }

    if (config.socket_type == SOCK_STREAM) {
        if (listen(sockfd, 5) < 0) {
            log_error("Listen failed");
            close(sockfd);
            return 1;
        }
    }

    log_info("Server started on port %d", config.port);

    while (1) {
        char buffer[BUFFER_SIZE] = {0};
        struct sockaddr_in cliaddr;
        socklen_t len = sizeof(cliaddr);
        
        int connfd;
        if (config.socket_type == SOCK_STREAM) {
            connfd = accept(sockfd, (struct sockaddr *)&cliaddr, &len);
            if (connfd < 0) {
                log_error("Accept failed");
                continue;
            }
            recv(connfd, buffer, BUFFER_SIZE, 0);
        } else {
            recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
            connfd = sockfd;
        }

        // Парсим запрос
        char *username = NULL, *command = NULL;
        char *quote1 = strchr(buffer, '"');
        if (quote1) {
            char *quote2 = strchr(quote1+1, '"');
            if (quote2) {
                username = quote1+1;
                *quote2 = 0;
                
                char *quote3 = strchr(quote2+1, '"');
                if (quote3) {
                    char *quote4 = strchr(quote3+1, '"');
                    if (quote4) {
                        command = quote3+1;
                        *quote4 = 0;
                    }
                }
            }
        }

        if (!username || !command) {
            send(connfd, "1:\"Invalid request format\"", 26, 0);
            if (config.socket_type == SOCK_STREAM) close(connfd);
            continue;
        }

        // Проверяем пользователя
        if (!is_user_allowed(username)) {
            send(connfd, "1:\"User not allowed\"", 20, 0);
            if (config.socket_type == SOCK_STREAM) close(connfd);
            continue;
        }

        // Выполняем команду
        execute_command(command, connfd);

        if (config.socket_type == SOCK_STREAM) {
            close(connfd);
        }
    }

    close(sockfd);
    return 0;
}
