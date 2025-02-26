#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#define PACKET_BUFFER_SIZE 1500

int main(int argc, char *argv[])
{
    srand((time(NULL)));

    // check arguments
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <UDP listen port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Read the UDP port from input
    char *port = argv[1];

    // Code from Beej's guide page 83 and 84
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    int status = getaddrinfo(NULL, port, &hints, &res);

    if (status != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }

    // create a UDP socket
    int server_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    // bind the socket to given address
    bind(server_socket, res->ai_addr, res->ai_addrlen);

    // recv from the address
    struct sockaddr_storage sender_addr;
    socklen_t sender_addr_len = sizeof(sender_addr);
    char buffer[PACKET_BUFFER_SIZE];

    FILE *outputFile = NULL;
    char receivedFileName[128] = {0};

    // main loop to receive file
    while (1)
    {
        // receive packet
        ssize_t bytes_received = recvfrom(server_socket, buffer, PACKET_BUFFER_SIZE, 0, (struct sockaddr *)&sender_addr, &sender_addr_len);
        if (bytes_received < 0)
        {
            perror("recvfrom");
            break;
        }

        // LOGIC TO DROP PACKETS SOMETIMES
        double number = (double)rand() / RAND_MAX; // number between zero and one
        if (number < 0.1)                          // 10% change of dropping a packet
        {
            printf("Packet dropped\n");
            continue; // don't send ACK
        }

        // format header from the packet
        int total_frag, frag_no, size;
        char filename[128];
        int header_length = 0;

        int items = sscanf(buffer, "%u:%u:%u:%127[^:]:%n",
                           &total_frag, &frag_no, &size, filename, &header_length);
        if (items != 4)
        {
            fprintf(stderr,
                    "Error parsing packet header. Packet contents:\n%.*s\n",
                    (int)bytes_received, buffer);
            continue;
        }

        printf("Received packet %u/%u (header %d bytes, data size %u bytes)\n", frag_no, total_frag, header_length, size);

        if (frag_no == 1)
        {
            strncpy(receivedFileName, filename, sizeof(receivedFileName) - 1);
            outputFile = fopen("finishedFile.jpeg", "wb"); // open create since it's first fragment
            if (!outputFile)
            {
                perror("fopen");
                break;
            }
            printf("Opened file '%s' for writing.\n", receivedFileName);
        }

        // write the file data
        if (size > 0)
        {
            size_t written = fwrite(buffer + header_length, 1, size, outputFile);
            if (written != size)
            {
                fprintf(stderr, "Error writing file data.\n");
                fclose(outputFile);
                break;
            }
        }

        // send ACK
        const char ack[] = "received";
        ssize_t ack_sent = sendto(server_socket, ack, strlen(ack), 0,
                                  (struct sockaddr *)&sender_addr, sender_addr_len);
        if (ack_sent < 0)
        {
            perror("sendto");
            fclose(outputFile);
            break;
        }
        printf("Sent ACK for packet %d\n", frag_no);

        if (frag_no == total_frag)
        {
            printf("File transfer completed. Saved as: %s\n", receivedFileName);
            fclose(outputFile);
            outputFile = NULL;
            break;
        }
    }

    close(server_socket);
    return 0;
}