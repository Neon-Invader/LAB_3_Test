#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <syslog.h>
#include <pwd.h>
#include "libmysyslog.h"
#include "rpc_server.h"

#define CONFIG_FILE "/etc/myRPC/myRPC.conf"
#define USERS_FILE  "/etc/myRPC/users.conf"
#define DEFAULT_PORT 1234
#define MAX_CLIENTS 10

// Глобальные переменные для демона
static volatile int keep_running = 1;

// Обработчик сигналов (для graceful shutdown)
void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        keep_running = 0;
    }
}

// Демонизация процесса
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // Родительский процесс завершается
    }
    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }
    // Закрываем стандартные дескрипторы
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[1024];
    int port = DEFAULT_PORT;
    int socket_type = SOCK_STREAM; // По умолчанию потоковый

    // Демонизация
    daemonize();

    // Настройка сигналов
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Чтение конфигурации
    if (parse_config(CONFIG_FILE, &port, &socket_type) {
        syslog(LOG_ERR, "Failed to parse config file");
        return EXIT_FAILURE;
    }

    // Создание сокета
    server_fd = socket(AF_INET, socket_type, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "Socket creation failed");
        return EXIT_FAILURE;
    }

    // Настройка адреса сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Привязка сокета
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Bind failed");
        close(server_fd);
        return EXIT_FAILURE;
    }

    // Прослушивание (для TCP)
    if (socket_type == SOCK_STREAM) {
        if (listen(server_fd, MAX_CLIENTS) < 0) {
            syslog(LOG_ERR, "Listen failed");
            close(server_fd);
            return EXIT_FAILURE;
        }
    }

    syslog(LOG_INFO, "Server started on port %d", port);

    // Основной цикл сервера
    while (keep_running) {
        if (socket_type == SOCK_STREAM) {
            client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                syslog(LOG_ERR, "Accept failed");
                continue;
            }
        } else {
            // UDP: используем server_fd для приёма
            client_fd = server_fd;
        }

        // Чтение запроса
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_read < 0) {
            syslog(LOG_ERR, "Recv failed");
            if (socket_type == SOCK_STREAM) close(client_fd);
            continue;
        }
        buffer[bytes_read] = '\0';

        // Обработка запроса (формат: "user:command")
        char* user = strtok(buffer, ":");
        char* command = strtok(NULL, "");
        if (!user || !command) {
            syslog(LOG_ERR, "Invalid request format");
            send_response(client_fd, "1:Invalid format");
            if (socket_type == SOCK_STREAM) close(client_fd);
            continue;
        }

        // Проверка пользователя
        if (!is_user_allowed(user, USERS_FILE)) {
            syslog(LOG_WARNING, "User %s not allowed", user);
            send_response(client_fd, "1:User not allowed");
            if (socket_type == SOCK_STREAM) close(client_fd);
            continue;
        }

        // Выполнение команды
        char* result = execute_command(command);
        send_response(client_fd, result);
        free(result);

        if (socket_type == SOCK_STREAM) close(client_fd);
    }

    close(server_fd);
    syslog(LOG_INFO, "Server stopped");
    return EXIT_SUCCESS;
}
