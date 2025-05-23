#ifndef RPC_SERVER_H
#define RPC_SERVER_H

int parse_config(const char* filename, int* port, int* socket_type);
int is_user_allowed(const char* user, const char* users_file);
char* execute_command(const char* command);
void send_response(int fd, const char* response);

#endif
