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

// Helper functions
void freeMemory(char **words);
void serialize_message(const struct message *msg, char *buffer, size_t buf_size);
void send_message(int sockfd, const char *message);
struct message deserialize_message(const char *serialized);
void add_client(struct client_info *new_client);
void handle_login(struct message msg, int client_socket);
struct session *session_check(const char *sessionID);
void handle_join(struct message msg, int client_socket);
void handle_leave(struct message msg, int client_socket);
void handle_new_session(struct message msg, int client_socket);
void handle_query(struct message msg, int client_socket);
void handle_message(struct message msg, int client_socket);

int main(int argc, char *argv[])
{
    // From Beej's guide 7.3
    fd_set master;
    fd_set read_fds;
    int fdmax;
    int client_socket;
    char buffer[1024];

    // check arguments
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
                    // new client it trying to join
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
                        // create new client_info struct
                        struct client_info *new_client = malloc(sizeof(struct client_info));
                        new_client->sockfd = client_socket;
                        strcpy(new_client->clientID, "unknown"); // login not yet processed
                        new_client->current_session = NULL;
                        new_client->next = NULL;
                        add_client(new_client);
                    }
                }
                // existing client is sending a message
                // existing client is sending a message
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
                            // Note: if a session is empty should it be deleled?
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
                        }
                    }
                }
            }
        }
    }
}

// Helper functions
void freeMemory(char **words)
{
    if (words == NULL)
        return; // Avoid double free
    for (int i = 0; words[i] != NULL; i++)
    {
        free(words[i]);
    }
    free(words);
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

    // Use a more robust parsing approach
    char type_str[20] = {0};
    char size_str[20] = {0};
    char source_buffer[MAX_NAME + 1] = {0};
    char *data_start = NULL;

    // Find the first three colons to extract fields
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

    // Find and extract source
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

    // Extract data (everything after the third colon)
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
    // Set values for login return message
    strncpy((char *)login_return.source, "server", MAX_NAME);
    login_return.source[MAX_NAME - 1] = '\0';

    // Default to failure
    login_return.type = LO_NAK;
    strncpy((char *)login_return.data, "User or password incorrect", MAX_DATA);
    login_return.data[MAX_DATA - 1] = '\0';
    login_return.size = strlen((char *)login_return.data);

    bool found = false;

    // Check if username and password are valid
    for (int i = 0; i < 4; i++)
    {
        printf("Checking account %s against %s\n", (char *)msg.source, accounts[i][0]);
        // Check if username exists
        if (strcmp((char *)msg.source, accounts[i][0]) == 0)
        {
            found = true;
            // Check if password is correct
            if (strcmp((char *)msg.data, accounts[i][1]) == 0)
            {
                login_return.type = LO_ACK;
                strncpy((char *)login_return.data, "Login successful", MAX_DATA);
                login_return.data[MAX_DATA - 1] = '\0';
                login_return.size = strlen((char *)login_return.data);
                printf("Login successful for %s\n", (char *)msg.source);
            }
            else
            {
                login_return.type = LO_NAK;
                strncpy((char *)login_return.data, "Incorrect password", MAX_DATA);
                login_return.data[MAX_DATA - 1] = '\0';
                login_return.size = strlen((char *)login_return.data);
                printf("Incorrect password for %s\n", (char *)msg.source);
            }
            break; // Found the username, no need to check further
        }
    }

    if (!found)
    {
        printf("User %s not found\n", (char *)msg.source);
    }

    // Serialize and send login return message
    char login_return_buffer[1024];
    serialize_message(&login_return, login_return_buffer, sizeof(login_return_buffer));
    printf("Sending response to socket %d: %s\n", client_socket, login_return_buffer);
    send(client_socket, login_return_buffer, strlen(login_return_buffer), 0);
}

/*
void handle_login(struct message msg, int client_socket)
{
    struct message login_return;
    // Set values for login return message
    strncpy((char *)login_return.source, "server", MAX_NAME);
    login_return.source[MAX_NAME - 1] = '\0';

    // Check if username and password are valid
    for (int i = 0; i < 4; i++)
    {
        // Check if username exists
        if (strcmp((const char *)msg.source, accounts[i][0]) == 0)
        {
            // Check if password is correct
            if (strcmp((const char *)msg.data, accounts[i][1]) == 0)
            {
                login_return.type = LO_ACK;
                strncpy((char *)login_return.data, "", MAX_DATA);
                login_return.data[MAX_DATA - 1] = '\0';
                login_return.size = 0;
                break;
            }
            else
            {
                login_return.type = LO_NAK;
                strncpy((char *)login_return.data, "Incorrect password", MAX_DATA);
                login_return.data[MAX_DATA - 1] = '\0';
                login_return.size = sizeof("Incorrect password");
                break;
            }
        }
        else
        {
            login_return.type = LO_NAK;
            strncpy((char *)login_return.data, "User or client does not exist", MAX_DATA);
            login_return.data[MAX_DATA - 1] = '\0';
            login_return.size = sizeof("User or client does not exist");
            break;
        }
    }

    // Serialize and send login return message
    char login_return_buffer[1024];
    serialize_message(&login_return, login_return_buffer, sizeof(login_return_buffer));
    send(client_socket, login_return_buffer, strlen(login_return_buffer), 0);
}
    */

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
    // Insert at beginning of participants list.
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
    { // Send JN_ACK
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
    // Note: should we do error checking like if client not found, etc.
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
    char info[MAX_DATA];

    // Add online users (be more careful with buffer limits)
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

    // Add sessions (be careful with buffer limits)
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
    // find which client sent message
    struct client_info *sender = clients_head;
    while (sender)
    {
        if (sender->sockfd == client_socket)
        {
            break;
        }
        sender = sender->next;
    }

    // check senders session
    struct session *curr_sess = sender->current_session;
    // Only sender is in session
    if (curr_sess->participants->next_participant == NULL)
    {
        return;
    }

    struct message return_message;
    return_message.type = MESSAGE;
    strncpy((char *)return_message.source, sender->clientID, MAX_NAME);
    return_message.source[MAX_NAME - 1] = '\0';

    // Send message to all in session
    char message_serialized[2048];
    strncpy(message_serialized, (char *)msg.data, sizeof(message_serialized));
    serialize_message(&return_message, message_serialized, sizeof(message_serialized));
    return_message.size = sizeof(return_message.data);

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