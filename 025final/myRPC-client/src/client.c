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
#define LOG_FILE_PATH "/var/log/myrpc.log"

typedef struct {
    char* remote_command;
    char* server_ip_address;
    int server_port_number;
    int connection_type;  // 1 = TCP, 0 = UDP
} ClientSettings;

void display_help_menu() {
    printf("Usage: myRPC-client [OPTIONS]\n");
    printf("Options:\n");
    printf("  -c, --command \"bash_command\"   Command to execute\n");
    printf("  -h, --host \"ip_addr\"          Server IP address\n");
    printf("  -p, --port PORT                Server port\n");
    printf("  -s, --stream                   Use stream socket (TCP)\n");
    printf("  -d, --dgram                    Use datagram socket (UDP)\n");
    printf("      --help                     Display this help and exit\n");
}

int process_arguments(int argc_count, char *argument_values[], ClientSettings *config) {
    static struct option long_options[] = {
        {"command", required_argument, 0, 'c'},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"stream", no_argument, 0, 's'},
        {"dgram", no_argument, 0, 'd'},
        {"help", no_argument, 0,  0 },
        {0, 0, 0, 0}
    };

    int option;
    while ((option = getopt_long(argc_count, argument_values, "c:h:p:sd", long_options, NULL)) != -1) {
        switch (option) {
            case 'c': config->remote_command = optarg; break;
            case 'h': config->server_ip_address = optarg; break;
            case 'p': config->server_port_number = atoi(optarg); break;
            case 's': config->connection_type = 1; break;
            case 'd': config->connection_type = 0; break;
            case 0: display_help_menu(); return 1;
            default: return -1;
        }
    }

    if (!config->remote_command || !config->server_ip_address || config->server_port_number <= 0) {
        fprintf(stderr, "Error: Required arguments missing\n");
        display_help_menu();
        return -1;
    }
    return 0;
}

int create_network_socket(int protocol_type) {
    int socket_type = protocol_type ? SOCK_STREAM : SOCK_DGRAM;
    int socket_descriptor = socket(AF_INET, socket_type, 0);
    if (socket_descriptor < 0) {
        mysyslog("Socket creation error", ERROR, 0, 0, LOG_FILE_PATH);
        perror("Socket creation error");
    }
    return socket_descriptor;
}

int establish_tcp_connection(int socket_desc, struct sockaddr_in *server_addr) {
    if (connect(socket_desc, (struct sockaddr*)server_addr, sizeof(*server_addr)) < 0) {
        mysyslog("Connection failed", ERROR, 0, 0, LOG_FILE_PATH);
        perror("Connection failed");
        close(socket_desc);
        return -1;
    }
    mysyslog("Server connection established", INFO, 0, 0, LOG_FILE_PATH);
    return 0;
}

void setup_server_address(const char *ip_address, int port_number, struct sockaddr_in *server_addr) {
    memset(server_addr, 0, sizeof(*server_addr));
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(port_number);
    inet_pton(AF_INET, ip_address, &server_addr->sin_addr);
}

int send_network_request(int socket_desc, const char *request_data, 
                        struct sockaddr_in *server_addr, int is_tcp) {
    ssize_t bytes_sent;
    if (is_tcp) {
        bytes_sent = send(socket_desc, request_data, strlen(request_data), 0);
    } else {
        bytes_sent = sendto(socket_desc, request_data, strlen(request_data), 0,
               (struct sockaddr*)server_addr, sizeof(*server_addr));
    }
    
    if (bytes_sent < 0) {
        mysyslog("Request send failed", ERROR, 0, 0, LOG_FILE_PATH);
        perror("Send error");
        return -1;
    }
    return 0;
}int receive_network_response(int socket_desc, char *response_buffer, 
                            struct sockaddr_in *server_addr, int is_tcp) {
    socklen_t address_length = sizeof(*server_addr);
    ssize_t bytes_received;
    
    if (is_tcp) {
        bytes_received = recv(socket_desc, response_buffer, BUFFER_SIZE - 1, 0);
    } else {
        bytes_received = recvfrom(socket_desc, response_buffer, BUFFER_SIZE - 1, 0, 
                          (struct sockaddr*)server_addr, &address_length);
    }
    
    if (bytes_received < 0) {
        mysyslog("Response receive failed", ERROR, 0, 0, LOG_FILE_PATH);
        perror("Receive error");
        return -1;
    }
    
    response_buffer[bytes_received] = '\0';
    return 0;
}

int main(int argument_count, char *argument_values[]) {
    ClientSettings client_config = {0};
    if (process_arguments(argument_count, argument_values, &client_config) != 0) {
        return EXIT_FAILURE;
    }

    struct passwd *user_info = getpwuid(getuid());
    char formatted_request[BUFFER_SIZE];
    snprintf(formatted_request, sizeof(formatted_request), 
           "%s: %s", user_info->pw_name, client_config.remote_command);

    mysyslog("Initiating server connection...", INFO, 0, 0, LOG_FILE_PATH);

    int main_socket = create_network_socket(client_config.connection_type);
    if (main_socket < 0) return EXIT_FAILURE;

    struct sockaddr_in server_address;
    setup_server_address(client_config.server_ip_address, 
                       client_config.server_port_number, &server_address);

    if (client_config.connection_type && 
        establish_tcp_connection(main_socket, &server_address) != 0) {
        return EXIT_FAILURE;
    }

    if (send_network_request(main_socket, formatted_request, &server_address, 
                           client_config.connection_type) != 0) {
        close(main_socket);
        return EXIT_FAILURE;
    }

    char server_response[BUFFER_SIZE];
    if (receive_network_response(main_socket, server_response, &server_address, 
                               client_config.connection_type) != 0) {
        close(main_socket);
        return EXIT_FAILURE;
    }

    printf("Server reply: %s\n", server_response);
    mysyslog("Server response received", INFO, 0, 0, LOG_FILE_PATH);

    close(main_socket);
    return EXIT_SUCCESS;
}
