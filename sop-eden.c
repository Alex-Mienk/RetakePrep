#include "l8_common.h"

#define BACKLOG 16
#define LOGIN_MAX 16
#define COMMAND_MAX 8
#define PARAM_MAX 10
#define MIN_SIZE (LOGIN_MAX + COMMAND_MAX)
#define CMDS 6

static char* COMMANDS[CMDS] = {"RUN", "EXIT", "PAUSE", "COMPUTE", "LIST", "GATHER"};

typedef struct {
    char login[LOGIN_MAX];
    char command[COMMAND_MAX];
    uint32_t params[PARAM_MAX];
} message_t;

int isValidMessage(message_t* message, int read_len)
{
    char login[LOGIN_MAX + 1];
    char command[COMMAND_MAX + 1];
    int isValidLogin = 0, isValidCMD = 0, isValidLen = 0;
    int login_len = 0, command_len = 0;

    if (read_len < MIN_SIZE || read_len > MSG_MAX) {
        printf("error: wrong message length %d\n", read_len);
        return 0;
    }

    while (login_len < LOGIN_MAX && message->login[login_len] != '\0')
        login_len++;
    while (command_len < COMMAND_MAX && message->command[command_len] != '\0')
        command_len++;

    memcpy(login, message->login, login_len);
    login[login_len] = '\0';
    memcpy(command, message->command, command_len);
    command[command_len] = '\0';

    for (int u = 0; u < USERS; u++) {
        if (strcmp(login, LOGINS[u]) == 0) {
            isValidLogin = 1;
        }
    }

    if (!isValidLogin) {
        printf("error: unknown user %s\n", login);
        return 0;
    }

    for (int c = 0; c < CMDS; c++) {
        if (strcmp(command, COMMANDS[c]) == 0) {
            isValidCMD = 1;
        }
    }

    if (!isValidCMD) {
        printf("error: unknown command %s\n", command);
        return 0;
    }

    if (strcmp(command, "COMPUTE") == 0) {
        if ((read_len - MIN_SIZE) > 0 && (read_len - MIN_SIZE) % 8 == 0) {
            isValidLen = 1;
        }
    } else {
        if (read_len == MIN_SIZE) {
            isValidLen = 1;
        }
    }

    if (!isValidLen) {
        printf("error: wrong message length %d\n", read_len);
        return 0;
    }

    return 1;
}

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

void handleExit()
{
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        usage(argv[0]);
    }

    int socketfd = bind_inet_socket(atoi(argv[1]), SOCK_DGRAM, BACKLOG);

    for (;;) {
        message_t message;
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);

        memset(&message, 0, sizeof(message_t));

        int count = recvfrom(socketfd, &message, sizeof(message_t), 0, (struct sockaddr*)&addr, &len);
        if (count < 0) {
            ERR("recvfrom");
        }

        if (!isValidMessage(&message, count)) {
            continue;
        }

        char login[LOGIN_MAX + 1];
        char command[COMMAND_MAX + 1];
        int login_len = 0, command_len = 0;

        while (login_len < LOGIN_MAX && message.login[login_len] != '\0')
            login_len++;
        while (command_len < COMMAND_MAX && message.command[command_len] != '\0')
            command_len++;

        memcpy(login, message.login, login_len);
        login[login_len] = '\0';
        memcpy(command, message.command, command_len);
        command[command_len] = '\0';

        printf("%s: %s", login, command);

        for (int p = 0; p < (count - MIN_SIZE) / 4; p++) {
            message.params[p] = ntohl(message.params[p]);
            printf(" %u", message.params[p]);
        }

        printf("\n");

        if (strcmp(command, "EXIT") == 0) {
            break;
        }
    }

    close(socketfd);
    return 0;
}
