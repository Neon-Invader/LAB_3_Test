#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <pwd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "libmysyslog.h"

#define BUFFER_SIZE 1024
#define LOG_FILE "/var/log/myrpc.log"

void print_help() {
    printf("Usage: myRPC-client [OPTIONS]\n");
    printf("Options:\n");
    printf("  -c, --command \"bash_command\"   Command to execute\n");
    printf("  -h, --host \"ip_addr\"          Server IP address\n");
    printf("  -p, --port PORT                Server port\n");
    printf("  -s, --stream                   Use stream socket (TCP)\n");
    printf("  -d, --dgram                    Use datagram socket (UDP)\n");
    printf("      --help                     Display this help and exit\n");
}

int main(int argc, char *argv[]) {
    char *command = NULL;
    char *server_ip = NULL;
    int port = 0;
    int use_tcp = 1;

    static struct option long_options[] = {
        {"command", required_argument, 0, 'c'},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"stream", no_argument, 0, 's'},
        {"dgram", no_argument, 0, 'd'},
        {"help", no_argument, 0, 0},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:h:p:sd", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c': command = optarg; break;
            case 'h': server_ip = optarg; break;
            case 'p': port = atoi(optarg); break;
            case 's': use_tcp = 1; break;
            case 'd': use_tcp = 0; break;
            case 0: print_help(); return 0;
            default: print_help(); return 1;
        }
    }

    if (!command || !server_ip || port <= 0) {
        fprintf(stderr, "Error: Required arguments missing\n");
        print_help();
        return 1;
    }

    struct passwd *user_info = getpwuid(getuid());
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s: %s", user_info->pw_name, command);

    mysyslog("Initializing client connection", INFO, 0, 0, LOG_FILE);

    int sockfd = socket(AF_INET, use_tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sockfd < 0) {
        mysyslog("Socket creation error", ERROR, 0, 0, LOG_FILE);
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);

    if (use_tcp) {
        if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            mysyslog("TCP connection failed", ERROR, 0, 0, LOG_FILE);
            perror("connect");
            close(sockfd);
            return 1;
        }
        mysyslog("TCP connection established", INFO, 0, 0, LOG_FILE);
    }

    // Отправка запроса с полной проверкой
    ssize_t bytes_sent;
    const size_t request_len = strlen(request);
    
    if (use_tcp) {
        bytes_sent = send(sockfd, request, request_len, 0);
    } else {
        bytes_sent = sendto(sockfd, request, request_len, 0,
                          (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    }

    if (bytes_sent != (ssize_t)request_len) {
        mysyslog("Data send error", ERROR, 0, 0, LOG_FILE);
        perror(bytes_sent < 0 ? "send" : "incomplete send");
        close(sockfd);
        return 1;
    }

    // Получение ответа с сохранением информации об отправителе для UDP
    char response[BUFFER_SIZE];
    ssize_t bytes_received;
    
    if (use_tcp) {
        bytes_received = recv(sockfd, response, BUFFER_SIZE - 1, 0);
    } else {
        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);
        bytes_received = recvfrom(sockfd, response, BUFFER_SIZE - 1, 0,
                                (struct sockaddr*)&sender_addr, &addr_len);
    }

    if (bytes_received < 0) {
        mysyslog("Response receive error", ERROR, 0, 0, LOG_FILE);
        perror("recv");
        close(sockfd);
        return 1;
    }

    response[bytes_received] = '\0';
    printf("Server response: %s\n", response);
    mysyslog("Received server response", INFO, 0, 0, LOG_FILE);

    close(sockfd);
    return 0;
}
