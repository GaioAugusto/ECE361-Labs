#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/select.h>

#define LOGIN 1
#define LO_ACK 2
#define LO_NAK 3
#define EXIT 4
#define JOIN 5
#define JN_ACK 6
#define JN_NAK 7
#define LEAVE_SESS 8
#define NEW_SESS 9
#define NS_ACK 10
#define MESSAGE 11
#define QUERY 12
#define QU_ACK 13

#define MAX_NAME 100
#define MAX_DATA 2048

struct message
{
    unsigned int type;
    unsigned int size;
    unsigned char source[MAX_NAME];
    unsigned char data[MAX_DATA];
};

int create_socket();
int connect_to_server(int sockfd, struct sockaddr_in *serverAddr);
void send_message(int sockfd, const char *message);
void receive_message(int sockfd, char *buffer, size_t buffer_size);
struct message create_message(unsigned int type, const char *source, const char *data);
void serialize_message(const struct message *msg, char *buffer, size_t buf_size);
struct message deserialize_message(const char *serialized);

int main(int argc, char *argv[])
{
    char clientID[64], password[64], serverIp[64];
    int serverPort;
    char command[16];
    int sockfd;

    // Login loop
    while (true)
    {
        printf("Please enter your command in the format:\n"
               "/login <clientID> <password> <serverIP> <server-port>\n");

        char userInput[128];
        if (!fgets(userInput, sizeof(userInput), stdin))
        {
            fprintf(stderr, "Error reading input.\n");
            return EXIT_FAILURE;
        }
        userInput[strcspn(userInput, "\n")] = '\0';

        int scanned = sscanf(userInput, "%s %s %s %s %d",
                             command, clientID, password, serverIp, &serverPort);
        if (scanned != 5 || strcmp(command, "/login") != 0)
        {
            fprintf(stderr, "Invalid command. Expected format:\n"
                            "/login <clientID> <password> <serverIP> <server-port>\n");
            continue;
        }

        // Set up server address structure
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPort);
        if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) <= 0)
        {
            perror("inet_pton");
            continue;
        }

        // Create and connect socket
        sockfd = create_socket();
        if (connect_to_server(sockfd, &serverAddr) < 0)
        {
            close(sockfd);
            continue;
        }

        // Create login message, serialize and send it
        struct message loginMsg = create_message(LOGIN, clientID, password);
        char serialized_login[1024];
        serialize_message(&loginMsg, serialized_login, sizeof(serialized_login));
        send_message(sockfd, serialized_login);

        // Wait for server response
        char buffer[1024];
        receive_message(sockfd, buffer, sizeof(buffer));
        struct message login_response = deserialize_message(buffer);
        if (login_response.type == LO_ACK)
        {
            printf("Login successful\n");
            break; // Exit the login loop
        }
        else if (login_response.type == LO_NAK)
        {
            printf("Login failed: %s\n", login_response.data);
            close(sockfd);
            continue;
        }
    }

    // Main event loop using select()
    fd_set read_fds;
    int fdmax;
    if (sockfd > STDIN_FILENO)
    {
        fdmax = sockfd;
    }
    else
    {
        fdmax = STDIN_FILENO;
    }
    printf("Enter command: ");
    fflush(stdout);

    while (true)
    {
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int activity = select(fdmax + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0)
        {
            perror("select");
            break;
        }

        // If there is data from the server socket:
        if (FD_ISSET(sockfd, &read_fds))
        {
            char recv_buffer[2048];
            ssize_t bytes = recv(sockfd, recv_buffer, sizeof(recv_buffer) - 1, 0);
            if (bytes <= 0)
            {
                if (bytes == 0)
                    printf("\nServer closed connection.\n");
                else
                    perror("recv");
                break;
            }
            recv_buffer[bytes] = '\0';
            struct message msg = deserialize_message(recv_buffer);
            // Process based on message type
            if (msg.type == MESSAGE)
                printf("\n[Message from %s]: %s\n", msg.source, msg.data);
            else if (msg.type == JN_ACK)
                printf("\nJoined session: %s\n", msg.data);
            else if (msg.type == JN_NAK)
                printf("\nFailed to join session: %s\n", msg.data);
            else if (msg.type == NS_ACK)
                printf("\nCreated session: %s\n", msg.data);
            else if (msg.type == QU_ACK)
                printf("\nQuery response: %s\n", msg.data);
            else
                printf("\n[Server]: %s\n", msg.data);

            printf("Enter command: ");
            fflush(stdout);
        }

        // If there is data from standard input:
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            char input_buffer[2048];
            if (!fgets(input_buffer, sizeof(input_buffer), stdin))
            {
                perror("fgets");
                continue;
            }
            input_buffer[strcspn(input_buffer, "\n")] = '\0';

            // Process commands
            if (strcmp(input_buffer, "/logout") == 0)
            {
                struct message logout = create_message(EXIT, clientID, "");
                char serialized_logout[2048];
                serialize_message(&logout, serialized_logout, sizeof(serialized_logout));
                send_message(sockfd, serialized_logout);
                printf("Logged out successfully\n");
                break;
            }
            else if (strcmp(input_buffer, "/leavesession") == 0)
            {
                struct message response = create_message(LEAVE_SESS, clientID, "");
                char serialized[1024];
                serialize_message(&response, serialized, sizeof(serialized));
                send_message(sockfd, serialized);
                printf("Left session\n");
            }
            else if (strncmp(input_buffer, "/joinsession ", 13) == 0)
            {
                char sessionID[MAX_DATA];
                strcpy(sessionID, input_buffer + 13);
                struct message response = create_message(JOIN, clientID, sessionID);
                char serialized[1024];
                serialize_message(&response, serialized, sizeof(serialized));
                send_message(sockfd, serialized);
            }
            else if (strncmp(input_buffer, "/createsession ", 15) == 0)
            {
                char sessionID[MAX_DATA];
                strcpy(sessionID, input_buffer + 15);
                struct message response = create_message(NEW_SESS, clientID, sessionID);
                char serialized[1024];
                serialize_message(&response, serialized, sizeof(serialized));
                send_message(sockfd, serialized);
            }
            else if (strcmp(input_buffer, "/list") == 0)
            {
                struct message response = create_message(QUERY, clientID, "");
                char serialized[1024];
                serialize_message(&response, serialized, sizeof(serialized));
                send_message(sockfd, serialized);
            }
            else if (strcmp(input_buffer, "/quit") == 0)
            {
                printf("Quitting\n");
                break;
            }
            else
            {
                // Regular chat message
                struct message response = create_message(MESSAGE, clientID, input_buffer);
                char serialized[2048];
                serialize_message(&response, serialized, sizeof(serialized));
                send_message(sockfd, serialized);
            }
            printf("Enter command: ");
            fflush(stdout);
        }
    }

    close(sockfd);
    return 0;
}

int create_socket()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

int connect_to_server(int sockfd, struct sockaddr_in *serverAddr)
{
    if (connect(sockfd, (struct sockaddr *)serverAddr, sizeof(*serverAddr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return -1;
    }
    return 0;
}

void send_message(int sockfd, const char *message)
{
    ssize_t bytesSent = send(sockfd, message, strlen(message), 0);
    if (bytesSent < 0)
    {
        perror("send");
    }
}

void receive_message(int sockfd, char *buffer, size_t buffer_size)
{
    memset(buffer, 0, buffer_size);
    ssize_t bytesRecv = recv(sockfd, buffer, buffer_size - 1, 0);
    if (bytesRecv < 0)
    {
        perror("recv");
    }
    buffer[bytesRecv] = '\0';
    printf("Server: %s\n", buffer);
}

struct message create_message(unsigned int type, const char *source, const char *data)
{
    struct message msg;
    msg.type = type;
    msg.size = (unsigned int)strlen(data);
    strncpy((char *)msg.source, source, MAX_NAME);
    msg.source[MAX_NAME - 1] = '\0';
    strncpy((char *)msg.data, data, MAX_DATA);
    msg.data[MAX_DATA - 1] = '\0';
    return msg;
}

void serialize_message(const struct message *msg, char *buffer, size_t buf_size)
{
    snprintf(buffer, buf_size, "%u:%u:%s:%s", msg->type, msg->size, msg->source, msg->data);
}

struct message deserialize_message(const char *serialized)
{
    struct message msg;
    memset(&msg, 0, sizeof(struct message)); // Initialize all fields to zero

    if (!serialized || strlen(serialized) == 0)
    {
        fprintf(stderr, "Error: Empty or NULL serialized message.\n");
        return msg;
    }

    char type_str[20] = {0};
    char size_str[20] = {0};

    // Find the first colon to extract type
    char *first_colon = strchr(serialized, ':');
    if (!first_colon)
    {
        fprintf(stderr, "Error: Invalid message format (missing type delimiter).\n");
        return msg;
    }
    int type_len = first_colon - serialized;
    if (type_len > 0 && type_len < 20)
    {
        strncpy(type_str, serialized, type_len);
        type_str[type_len] = '\0';
        msg.type = atoi(type_str);
    }

    // Find the second colon to extract size
    char *second_colon = strchr(first_colon + 1, ':');
    if (!second_colon)
    {
        fprintf(stderr, "Error: Invalid message format (missing size delimiter).\n");
        return msg;
    }
    int size_len = second_colon - (first_colon + 1);
    if (size_len > 0 && size_len < 20)
    {
        strncpy(size_str, first_colon + 1, size_len);
        size_str[size_len] = '\0';
        msg.size = atoi(size_str);
    }

    // Find the third colon to extract source
    char *third_colon = strchr(second_colon + 1, ':');
    if (!third_colon)
    {
        fprintf(stderr, "Error: Invalid message format (missing source delimiter).\n");
        return msg;
    }
    int source_len = third_colon - (second_colon + 1);
    if (source_len > 0 && source_len < MAX_NAME)
    {
        strncpy((char *)msg.source, second_colon + 1, source_len);
        msg.source[source_len] = '\0';
    }

    // The remainder is the data
    char *data_start = third_colon + 1;
    if (data_start && *data_start)
    {
        strncpy((char *)msg.data, data_start, MAX_DATA - 1);
        msg.data[MAX_DATA - 1] = '\0';
    }
    return msg;
}