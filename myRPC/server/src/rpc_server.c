#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "libmysyslog.h"  // Подключение библиотеки логирования
#include "rpc_server.h"

int parse_config(const char* filename, int* port, int* socket_type) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        mysyslog(LOG_ERR, "Failed to open config file: %s", strerror(errno));
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (strstr(line, "port =")) {
            *port = atoi(strchr(line, '=') + 1);
        } else if (strstr(line, "socket_type =")) {
            if (strstr(line, "stream")) *socket_type = SOCK_STREAM;
            else if (strstr(line, "dgram")) *socket_type = SOCK_DGRAM;
        }
    }
    fclose(file);
    return 0;
}

int is_user_allowed(const char* user, const char* users_file) {
    FILE* file = fopen(users_file, "r");
    if (!file) {
        mysyslog(LOG_ERR, "Failed to open users file: %s", strerror(errno));
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, user) == 0) {
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}

char* execute_command(const char* command) {
    // Создание временных файлов
    char stdout_path[] = "/tmp/myRPC_XXXXXX.stdout";
    char stderr_path[] = "/tmp/myRPC_XXXXXX.stderr";
    int stdout_fd = mkstemps(stdout_path, 7);
    int stderr_fd = mkstemps(stderr_path, 7);

    if (stdout_fd < 0 || stderr_fd < 0) {
        mysyslog(LOG_ERR, "Failed to create temp files: %s", strerror(errno));
        return strdup("1:Failed to create temp files");
    }

    pid_t pid = fork();
    if (pid < 0) {
        mysyslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        close(stdout_fd);
        close(stderr_fd);
        return strdup("1:Fork failed");
    }

    if (pid == 0) {  // Дочерний процесс
        dup2(stdout_fd, STDOUT_FILENO);
        dup2(stderr_fd, STDERR_FILENO);
        execl("/bin/sh", "sh", "-c", command, NULL);
        mysyslog(LOG_ERR, "execl failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Родительский процесс
    close(stdout_fd);
    close(stderr_fd);
    waitpid(pid, NULL, 0);

    // Чтение результатов
    FILE* stdout_file = fopen(stdout_path, "r");
    FILE* stderr_file = fopen(stderr_path, "r");
    if (!stdout_file || !stderr_file) {
        mysyslog(LOG_ERR, "Failed to open output files: %s", strerror(errno));
        if (stdout_file) fclose(stdout_file);
        if (stderr_file) fclose(stderr_file);
        return strdup("1:Failed to read output");
    }

    // Определение успеха/ошибки
    fseek(stderr_file, 0, SEEK_END);
    long stderr_size = ftell(stderr_file);
    rewind(stderr_file);

    char* result;
    if (stderr_size > 0) {
        result = malloc(stderr_size + 3);
        if (!result) {
            mysyslog(LOG_ERR, "Memory allocation failed");
            fclose(stdout_file);
            fclose(stderr_file);
            return strdup("1:Memory error");
        }
        snprintf(result, 3, "1:");
        fread(result + 2, 1, stderr_size, stderr_file);
        result[stderr_size + 2] = '\0';
    } else {
        fseek(stdout_file, 0, SEEK_END);
        long stdout_size = ftell(stdout_file);
        rewind(stdout_file);
        result = malloc(stdout_size + 3);
        if (!result) {
            mysyslog(LOG_ERR, "Memory allocation failed");
            fclose(stdout_file);
            fclose(stderr_file);
            return strdup("1:Memory error");
        }
        snprintf(result, 3, "0:");
        fread(result + 2, 1, stdout_size, stdout_file);
        result[stdout_size + 2] = '\0';
    }

    fclose(stdout_file);
    fclose(stderr_file);
    remove(stdout_path);
    remove(stderr_path);
    return result;
}

void send_response(int fd, const char* response) {
    if (send(fd, response, strlen(response), 0) < 0) {
        mysyslog(LOG_ERR, "Failed to send response: %s", strerror(errno));
    }
}
