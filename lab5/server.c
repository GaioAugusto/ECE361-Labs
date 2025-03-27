#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>

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
#define REGISTER 14
#define REG_ACK 15
#define REG_NAK 16
#define PRIVATE_MESSAGE 17

#define MAX_NAME 100
#define MAX_DATA 1024

struct message
{
    unsigned int type;
    unsigned int size;
    unsigned char source[MAX_NAME];
    unsigned char data[MAX_DATA];
};

struct client_info;
struct session;

struct client_info
{
    int sockfd;
    char clientID[MAX_NAME];
    struct session *current_session;
    struct client_info *next;
    struct client_info *next_participant; // to help with linked list of participants in a session
};

struct session
{
    char sessionID[MAX_NAME];
    struct client_info *participants; // linked list
    struct session *next;
};

// Linked lists to track current active clients and sessions
struct client_info *clients_head = NULL;
struct session *sessions_head = NULL;

// Pre-existing clients
char accounts[4][2][50] = {
    {"client1", "password1"},
    {"client2", "password2"},
    {"client3", "password3"},
    {"client4", "password4"}};

// Function prototypes
void serialize_message(const struct message *msg, char *buffer, size_t buf_size);
void send_message(int sockfd, const char *message);
struct message deserialize_message(const char *serialized);
void add_client(struct client_info *new_client);
void handle_login(struct message msg, int client_socket);
struct session *session_check(const char *sessionID);
void add_to_session(struct session *sess, struct client_info *client);
void handle_join(struct message msg, int client_socket);
void handle_leave(struct message msg, int client_socket);
void handle_new_session(struct message msg, int client_socket);
void handle_query(struct message msg, int client_socket);
void handle_message(struct message msg, int client_socket);
int register_user(const char *username, const char *password);
int verify_login(const char *username, const char *password);
void handle_private_message(struct message msg, int client_socket);

int main(int argc, char *argv[])
{
    FILE *fp = fopen("accounts.txt", "r");
    if (fp == NULL)
    {
        // File doesn't exist
        fp = fopen("accounts.txt", "w");
        if (fp == NULL)
        {
            perror("Could not create accounts file");
            exit(EXIT_FAILURE);
        }
        fprintf(fp, "client1 password1\n");
        fprintf(fp, "client2 password2\n");
        fprintf(fp, "client3 password3\n");
        fprintf(fp, "client4 password4\n");
        fclose(fp);
    }
    else
    {
        fclose(fp);
    }

    // From Beej's guide 7.3
    fd_set master;
    fd_set read_fds;
    int fdmax;
    int client_socket;
    char buffer[1024];

    // Check arguments
    if (argc != 2)
    {
        printf("Error: Wrong number of parameters.\n");
        return EXIT_FAILURE;
    }

    // Read the TCP port from input
    char *port = argv[1];

    // Code from Beej's guide page 83 and 84
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // use IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0)
    {
        perror("getaddrinfo failed");
        return EXIT_FAILURE;
    }

    int server_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_socket == -1)
    {
        perror("socket creation failed");
        freeaddrinfo(res);
        return EXIT_FAILURE;
    }

    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)); // From Beej's guide

    // Bind socket to given address
    if (bind(server_socket, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("bind failed");
        close(server_socket);
        freeaddrinfo(res);
        return EXIT_FAILURE;
    }

    if (listen(server_socket, 10) == -1)
    {
        perror("listen failed");
        close(server_socket);
        return EXIT_FAILURE;
    }
    freeaddrinfo(res);

    // From Beej's guide 7.3
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(server_socket, &master);
    fdmax = server_socket;

    // Main loop for the server
    // From Beej's guide 7.3
    for (;;)
    {                      // basically a while loop
        read_fds = master; // copy master set
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i <= fdmax; i++)
        {
            if (FD_ISSET(i, &read_fds))
            {
                if (i == server_socket)
                {
                    // New client is trying to join
                    struct sockaddr_storage client_address;
                    socklen_t client_address_size = sizeof(client_address);
                    client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_size);
                    if (client_socket == -1)
                    {
                        perror("accept failed");
                    }
                    else
                    {
                        FD_SET(client_socket, &master);
                        if (client_socket > fdmax)
                        {
                            fdmax = client_socket;
                        }
                        // Create new client_info struct
                        struct client_info *new_client = malloc(sizeof(struct client_info));
                        new_client->sockfd = client_socket;
                        strcpy(new_client->clientID, "unknown"); // login not yet processed
                        new_client->current_session = NULL;
                        new_client->next = NULL;
                        add_client(new_client);
                    }
                }
                else
                {
                    int n = recv(i, buffer, sizeof(buffer) - 1, 0);
                    if (n <= 0)
                    {
                        // Client closed connection or error
                        if (n == 0)
                        {
                            printf("Client socket %d hung up\n", i);
                        }
                        else
                        {
                            perror("recv failed");
                        }
                        close(i);
                        FD_CLR(i, &master);

                        // Remove client from list
                        struct client_info **curr = &clients_head;
                        while (*curr)
                        {
                            if ((*curr)->sockfd == i)
                            {
                                struct client_info *temp = *curr;
                                *curr = (*curr)->next;
                                free(temp);
                                break;
                            }
                            curr = &(*curr)->next;
                        }
                    }
                    else
                    {
                        buffer[n] = '\0'; // Null terminate buffer

                        // Print received message for debugging
                        printf("Received from socket %d: %s\n", i, buffer);

                        // Convert incoming to struct
                        struct message msg = deserialize_message(buffer);

                        // Process message based on type
                        switch (msg.type)
                        {
                        case REGISTER:
                        {
                            int res = register_user((char *)msg.source, (char *)msg.data);
                            struct message reg_return;
                            strncpy((char *)reg_return.source, "server", MAX_NAME);
                            reg_return.source[MAX_NAME - 1] = '\0';
                            if (res == 0)
                            {
                                reg_return.type = REG_ACK;
                                strncpy((char *)reg_return.data, "Registration successful", MAX_DATA);
                            }
                            else
                            {
                                reg_return.type = REG_NAK;
                                strncpy((char *)reg_return.data, "Registration failed: username exists or error", MAX_DATA);
                            }
                            reg_return.data[MAX_DATA - 1] = '\0';
                            reg_return.size = strlen((char *)reg_return.data);

                            char reg_return_buffer[1024];
                            serialize_message(&reg_return, reg_return_buffer, sizeof(reg_return_buffer));
                            send(client_socket, reg_return_buffer, strlen(reg_return_buffer), 0);
                            break;
                        }
                        case LOGIN:
                        {
                            // Update client info and handle login
                            struct client_info *cli = clients_head;
                            // Update the client ID
                            while (cli)
                            {
                                if (cli->sockfd == i)
                                {
                                    strcpy(cli->clientID, (char *)msg.source);
                                    break;
                                }
                                cli = cli->next;
                            }
                            handle_login(msg, i);
                            break;
                        }
                        case EXIT:
                        {
                            // Remove client from list
                            struct client_info **curr = &clients_head;
                            while (*curr)
                            {
                                if ((*curr)->sockfd == i)
                                {
                                    struct client_info *temp = *curr;
                                    *curr = (*curr)->next;
                                    free(temp);
                                    break;
                                }
                                curr = &(*curr)->next;
                            }
                            close(i);
                            FD_CLR(i, &master);
                            break;
                        }
                        case JOIN:
                        {
                            // Join client to a session
                            handle_join(msg, i);
                            break;
                        }
                        case LEAVE_SESS:
                        {
                            // Remove client from session
                            handle_leave(msg, i);
                            break;
                        }
                        case NEW_SESS:
                        {
                            handle_new_session(msg, i);
                            break;
                        }
                        case QUERY:
                        {
                            handle_query(msg, i);
                            break;
                        }
                        case MESSAGE:
                        {
                            handle_message(msg, i);
                            break;
                        }
                        case PRIVATE_MESSAGE:
                        {
                            handle_private_message(msg, i);
                            break;
                        }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

void serialize_message(const struct message *msg, char *buffer, size_t buf_size)
{
    snprintf(buffer, buf_size, "%u:%u:%s:%s", msg->type, msg->size, msg->source, msg->data);
}

struct message deserialize_message(const char *serialized)
{
    struct message msg;
    memset(&msg, 0, sizeof(struct message)); // Initialize all fields to zero

    // Check if the serialized string is valid
    if (!serialized || strlen(serialized) == 0)
    {
        fprintf(stderr, "Error: Empty or NULL serialized message.\n");
        return msg;
    }

    char type_str[20] = {0};
    char size_str[20] = {0};
    char source_buffer[MAX_NAME + 1] = {0};
    char *data_start = NULL;

    char *first_colon = strchr(serialized, ':');
    if (!first_colon)
    {
        fprintf(stderr, "Error: Invalid message format (missing type delimiter).\n");
        return msg;
    }

    // Extract type
    int type_len = first_colon - serialized;
    if (type_len > 0 && type_len < 20)
    {
        strncpy(type_str, serialized, type_len);
        type_str[type_len] = '\0';
        msg.type = atoi(type_str);
    }

    // Find and extract size
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

    data_start = third_colon + 1;
    if (data_start && *data_start)
    {
        strncpy((char *)msg.data, data_start, MAX_DATA - 1);
        msg.data[MAX_DATA - 1] = '\0';
    }

    return msg;
}

void send_message(int sockfd, const char *message)
{
    ssize_t bytesSent = send(sockfd, message, strlen(message), 0);
    if (bytesSent < 0)
    {
        perror("send");
    }
}

void add_client(struct client_info *new_client)
{
    new_client->next = clients_head;
    clients_head = new_client;
}

void handle_login(struct message msg, int client_socket)
{
    struct message login_return;
    // Set source field to server
    strncpy((char *)login_return.source, "server", MAX_NAME);
    login_return.source[MAX_NAME - 1] = '\0';

    // Default failure message
    login_return.type = LO_NAK;
    strncpy((char *)login_return.data, "User or password incorrect", MAX_DATA);
    login_return.data[MAX_DATA - 1] = '\0';
    login_return.size = strlen((char *)login_return.data);

    // Use the txt verification
    if (verify_login((char *)msg.source, (char *)msg.data) == 0)
    {
        // Login successful
        login_return.type = LO_ACK;
        strncpy((char *)login_return.data, "Login successful", MAX_DATA);
        login_return.data[MAX_DATA - 1] = '\0';
        login_return.size = strlen((char *)login_return.data);
        printf("Login successful for %s\n", (char *)msg.source);
    }
    else
    {
        printf("Login failed for %s\n", (char *)msg.source);
    }

    // Serialize and send login return message
    char login_return_buffer[1024];
    serialize_message(&login_return, login_return_buffer, sizeof(login_return_buffer));
    printf("Sending response to socket %d: %s\n", client_socket, login_return_buffer);
    send(client_socket, login_return_buffer, strlen(login_return_buffer), 0);
}

struct session *session_check(const char *sessionID)
{
    struct session *curr = sessions_head;
    while (curr)
    {
        if (strcmp(curr->sessionID, sessionID) == 0)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

void add_to_session(struct session *sess, struct client_info *client)
{
    // Insert at beginning of participants list
    client->next = sess->participants;
    sess->participants = client;
}

void handle_join(struct message msg, int client_socket)
{
    struct message join_return;
    strncpy((char *)join_return.source, "server", MAX_NAME);
    join_return.source[MAX_NAME - 1] = '\0';

    struct session *checker = session_check((char *)msg.data);
    if (checker == NULL)
    {
        // Send JN_NAK
        join_return.type = JN_NAK;
        strncpy((char *)join_return.data, "Session does not exist", MAX_DATA);
        join_return.data[MAX_DATA - 1] = '\0';
        join_return.size = sizeof("Session does not exist");
    }
    else
    {
        // Send JN_ACK
        join_return.type = JN_ACK;
        strncpy((char *)join_return.data, checker->sessionID, MAX_DATA);
        join_return.data[MAX_DATA - 1] = '\0';
        join_return.size = sizeof(checker->sessionID);

        // Add client to existing session
        struct client_info *curr_client = clients_head;
        while (curr_client)
        {
            if (curr_client->sockfd == client_socket)
            {
                curr_client->current_session = checker;
                curr_client->next_participant = checker->participants;
                checker->participants = curr_client;
                break;
            }
            curr_client = curr_client->next;
        }
    }

    char join_return_buffer[1024];
    serialize_message(&join_return, join_return_buffer, sizeof(join_return_buffer));
    send(client_socket, join_return_buffer, strlen(join_return_buffer), 0);
}

void handle_leave(struct message msg, int client_socket)
{
    struct client_info *client = clients_head;
    while (client)
    {
        if (client->sockfd == client_socket)
        {
            break;
        }
        client = client->next;
    }

    struct session *curr_session = client->current_session;
    // Remove client from participants list
    struct client_info **curr = &(curr_session->participants);
    while (*curr)
    {
        if ((*curr)->sockfd == client_socket)
        {
            struct client_info *temp = *curr;
            *curr = (*curr)->next_participant;
            break;
        }
        curr = &(*curr)->next_participant;
    }
    client->current_session = NULL;

    // Check if session is empty
    if (curr_session->participants == NULL)
    {
        // Remove session from list
        struct session **temp = &sessions_head;
        while (*temp)
        {
            if (*temp == curr_session)
            {
                *temp = curr_session->next;
                free(curr_session);
                break;
            }
            temp = &(*temp)->next;
        }
    }
}

void handle_new_session(struct message msg, int client_socket)
{
    struct message new_session_return;
    strncpy((char *)new_session_return.source, "server", MAX_NAME);
    new_session_return.source[MAX_NAME - 1] = '\0';
    new_session_return.size = 0;
    strncpy((char *)new_session_return.data, "", MAX_DATA);
    new_session_return.data[MAX_DATA - 1] = '\0';
    new_session_return.type = NS_ACK;

    struct session *newSession = malloc(sizeof(struct session));
    strncpy(newSession->sessionID, (char *)msg.data, MAX_NAME);
    newSession->sessionID[MAX_NAME - 1] = '\0';
    newSession->participants = NULL;

    newSession->next = sessions_head;
    sessions_head = newSession;

    // Add client to this new session
    struct client_info *curr_client = clients_head;
    while (curr_client)
    {
        if (curr_client->sockfd == client_socket)
        {
            curr_client->current_session = newSession;
            curr_client->next_participant = newSession->participants;
            newSession->participants = curr_client;
            break;
        }
        curr_client = curr_client->next;
    }

    char return_buffer[1024];
    serialize_message(&new_session_return, return_buffer, sizeof(return_buffer));
    send(client_socket, return_buffer, strlen(return_buffer), 0);
}

void handle_query(struct message msg, int client_socket)
{
    struct message query_return;
    strncpy((char *)query_return.source, "server", MAX_NAME);
    query_return.source[MAX_NAME - 1] = '\0';
    query_return.type = QU_ACK;

    // Create a smaller info buffer that will fit in MAX_DATA
    char info[MAX_DATA] = "";
    strcat(info, "Users: ");

    struct client_info *cli = clients_head;
    while (cli && strlen(info) < MAX_DATA - 50)
    { // Leave room for more text
        if (strlen(cli->clientID) + strlen(info) < MAX_DATA - 10)
        {
            strcat(info, cli->clientID);
            strcat(info, ", ");
        }
        cli = cli->next;
    }

    if (strlen(info) < MAX_DATA - 20)
    {
        strcat(info, "\nSessions: ");
        struct session *sess = sessions_head;
        while (sess && strlen(info) < MAX_DATA - 30)
        {
            if (strlen(sess->sessionID) + strlen(info) < MAX_DATA - 5)
            {
                strcat(info, sess->sessionID);
                strcat(info, ", ");
            }
            sess = sess->next;
        }
    }

    strncpy((char *)query_return.data, info, MAX_DATA);
    query_return.data[MAX_DATA - 1] = '\0';
    query_return.size = strlen((char *)query_return.data);

    char return_buffer[MAX_DATA + 200]; // Larger buffer for serialized message
    serialize_message(&query_return, return_buffer, sizeof(return_buffer));
    send(client_socket, return_buffer, strlen(return_buffer), 0);
}

void handle_message(struct message msg, int client_socket)
{
    struct client_info *sender = clients_head;
    while (sender)
    {
        if (sender->sockfd == client_socket)
        {
            break;
        }
        sender = sender->next;
    }

    struct session *curr_sess = sender->current_session;
    if (curr_sess == NULL || curr_sess->participants == NULL)
    {
        return;
    }

    if (curr_sess->participants->next_participant == NULL && curr_sess->participants == sender)
    {
        return;
    }

    struct message return_message;
    return_message.type = MESSAGE;
    strncpy((char *)return_message.source, sender->clientID, MAX_NAME);
    return_message.source[MAX_NAME - 1] = '\0';
    strncpy((char *)return_message.data, (char *)msg.data, MAX_DATA);
    return_message.data[MAX_DATA - 1] = '\0';
    return_message.size = strlen((char *)return_message.data);

    // Serialize the message
    char message_serialized[2048];
    serialize_message(&return_message, message_serialized, sizeof(message_serialized));

    // Send to all other clients in the session
    struct client_info *curr_client = curr_sess->participants;
    while (curr_client)
    {
        if (curr_client->sockfd != client_socket)
        {
            send(curr_client->sockfd, message_serialized, strlen(message_serialized), 0);
        }
        curr_client = curr_client->next_participant;
    }
}

int register_user(const char *username, const char *password)
{
    // a+ gives us reading/appending permission
    FILE *filePosition = fopen("accounts.txt", "a+");
    if (filePosition == NULL)
    {
        perror("Failed to open file\n");
        return -1;
    }

    rewind(filePosition);
    char line[256];
    while (fgets(line, sizeof(line), filePosition) != NULL)
    {
        char file_user[128], file_password[128];
        if (sscanf(line, "%127s %127s", file_user, file_password) == 2)
        {
            // Check if username already exists
            if (strcmp(username, file_user) == 0)
            {
                fclose(filePosition);
                return -1;
            }
        }
    }

    fprintf(filePosition, "%s %s\n", username, password);
    fclose(filePosition);
    return 0; // success
}

int verify_login(const char *username, const char *password)
{
    FILE *fp = fopen("accounts.txt", "r");
    if (!fp)
    {
        perror("fopen");
        return -1;
    }
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char file_user[128], file_pass[128];
        if (sscanf(line, "%127s %127s", file_user, file_pass) == 2)
        {
            if (strcmp(username, file_user) == 0 &&
                strcmp(password, file_pass) == 0)
            {
                fclose(fp);
                return 0;
            }
        }
    }
    fclose(fp);
    return -1;
}

void handle_private_message(struct message msg, int client_socket)
{
    printf("Got PM from socket %d: %s\n", client_socket, msg.data);

    char targetID[MAX_NAME] = {0};
    char pm_message[MAX_DATA] = {0};
    char *backslash = strstr((char *)msg.data, "\\ "); // Find the separator "\ "
    if (!backslash)
    {
        printf("Bad PM format: %s\n", msg.data);
        struct message error = {MESSAGE, 0, "server", "Invalid PM format", 0};
        error.size = strlen((char *)error.data);
        char serialized[2048];
        serialize_message(&error, serialized, sizeof(serialized));
        send(client_socket, serialized, strlen(serialized), 0);
        return;
    }

    int target_len = backslash - (char *)msg.data;
    if (target_len >= MAX_NAME)
        target_len = MAX_NAME - 1;
    strncpy(targetID, (char *)msg.data, target_len);
    targetID[target_len] = '\0';
    strncpy(pm_message, backslash + 2, MAX_DATA - 1); // Skip "\ "
    pm_message[MAX_DATA - 1] = '\0';

    printf("Target: %s, Message: %s\n", targetID, pm_message);

    struct client_info *sender = clients_head;
    while (sender && sender->sockfd != client_socket)
        sender = sender->next;
    if (!sender)
    {
        printf("Sender not found\n");
        return;
    }

    struct client_info *target = clients_head;
    while (target)
    {
        if (strcmp(target->clientID, targetID) == 0 && target->sockfd != client_socket)
        {
            struct message pm = {PRIVATE_MESSAGE, 0, "", "", 0};
            pm.type = PRIVATE_MESSAGE;
            strncpy((char *)pm.source, sender->clientID, MAX_NAME);
            pm.source[MAX_NAME - 1] = '\0';
            strncpy((char *)pm.data, pm_message, MAX_DATA);
            pm.data[MAX_DATA - 1] = '\0';
            pm.size = strlen((char *)pm.data);

            char serialized[2048];
            serialize_message(&pm, serialized, sizeof(serialized));
            printf("Sending to %s (sock %d): %s\n", target->clientID, target->sockfd, serialized);
            if (send(target->sockfd, serialized, strlen(serialized), 0) < 0)
            {
                perror("Send failed");
            }
            return;
        }
        target = target->next;
    }

    printf("Target %s not found\n", targetID);
    struct message error = {MESSAGE, 0, "server", "Target not found", 0};
    error.size = strlen((char *)error.data);
    char serialized[2048];
    serialize_message(&error, serialized, sizeof(serialized));
    send(client_socket, serialized, strlen(serialized), 0);
}