#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pwd.h>
#include <libmysyslog.h>

#define BUFFER_SIZE 4096
#define DEFAULT_PORT 1234
#define DEFAULT_HOST "127.0.0.1"

void print_help() {
    printf("Usage: myRPC-client -c \"command\" [-h host] [-p port] [-s|-d]\n");
    printf("Options:\n");
    printf("  -c, --command \"cmd\"  Bash command to execute\n");
    printf("  -h, --host addr     Server IP (default: %s)\n", DEFAULT_HOST);
    printf("  -p, --port num      Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -s, --stream        Use stream socket (TCP)\n");
    printf("  -d, --dgram        Use datagram socket (UDP)\n");
    printf("  --help              Show this help\n");
}

int main(int argc, char *argv[]) {
    char *command = NULL;
    char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    int socket_type = SOCK_STREAM;
    int opt;
    
    // Парсинг аргументов
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--command") == 0) {
            if (i+1 < argc) command = argv[++i];
        } 
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) {
            if (i+1 < argc) host = argv[++i];
        }
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i+1 < argc) port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stream") == 0) {
            socket_type = SOCK_STREAM;
        }
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dgram") == 0) {
            socket_type = SOCK_DGRAM;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
    }
    
    if (!command) {
        fprintf(stderr, "Error: Command is required\n");
        print_help();
        return 1;
    }

    // Получаем текущего пользователя
    struct passwd *pw = getpwuid(getuid());
    if (!pw) {
        log_error("Failed to get username");
        return 1;
    }
    
    // Формируем сообщение в текстовом протоколе
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "\"%s\":\"%s\"", pw->pw_name, command);

    // Создаем сокет
    int sockfd = socket(AF_INET, socket_type, 0);
    if (sockfd < 0) {
        log_error("Socket creation failed");
        return 1;
    }

    // Настраиваем адрес сервера
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &servaddr.sin_addr) <= 0) {
        log_error("Invalid address / Address not supported");
        close(sockfd);
        return 1;
    }

    // Подключаемся к серверу
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        log_error("Connection failed");
        close(sockfd);
        return 1;
    }

    // Отправляем сообщение
    if (send(sockfd, message, strlen(message), 0) < 0) {
        log_error("Message send failed");
        close(sockfd);
        return 1;
    }

    // Получаем ответ
    char buffer[BUFFER_SIZE] = {0};
    if (recv(sockfd, buffer, BUFFER_SIZE, 0) < 0) {
        log_error("Failed to receive response");
        close(sockfd);
        return 1;
    }

    // Парсим ответ сервера
    int code = -1;
    char *result = strchr(buffer, '"');
    if (sscanf(buffer, "%d:", &code) == 1 && result) {
        result = strtok(result+1, "\"");
        printf("Server response [code %d]:\n%s\n", code, result ? result : "No output");
    } else {
        printf("Invalid server response: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}
