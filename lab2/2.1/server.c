#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

int main(int argc, char *argv[])
{
    // check arguments
    if (argc != 2)
    {
        printf("[Error] Not enough or too many arguments.\n");
        return (EXIT_FAILURE);
    }

    // Read the UDP port from input
    char *port = argv[1];

    // Code from Beej's guide page 83 and 84
    struct addrinfo hints;
    struct addrinfo *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, port, &hints, &res);

    int server_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    // bind socket to given address
    bind(server_socket, res->ai_addr, res->ai_addrlen);

    // recv from the address
    struct sockaddr_storage sender_address;
    socklen_t sender_address_size = sizeof(sender_address);
    char buffer[100];

    int n = recvfrom(server_socket, buffer, 100, 0, (struct sockaddr *)&sender_address, &sender_address_size);

    // null terminate buffer
    buffer[n] = '\0';

    // if ftp send yes
    if (strcmp(buffer, "ftp") == 0)
    {
        sendto(server_socket, "yes", strlen("yes"), 0, (struct sockaddr *)&sender_address, sender_address_size);
        printf("yes\n");
    }
    else
    {
        sendto(server_socket, "no", strlen("no"), 0, (struct sockaddr *)&sender_address, sender_address_size);
        printf("no");
    }

    close(server_socket);

    return 0;
}