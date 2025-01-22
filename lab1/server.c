#include <stdio.h>              // For printf, perror, etc.
#include <stdlib.h>             // For exit, atoi
#include <string.h>             // For memset, strcmp
#include <unistd.h>             // For close
#include <sys/types.h>          // For system data types (optional on some systems)
#include <sys/socket.h>         // For socket, bind, recvfrom, sendto
#include <arpa/inet.h>          // For inet_pton, htons, etc.
#include <netinet/in.h>         // For sockaddr_in

int main(int argc, char *argv[]) {
    // 1. Check usage: we want <server Port>
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <UDP listen port>\n", argv[0]);
        return 1;
    }

    // 2. Parse the port
    int listenPort = atoi(argv[1]);

    // 3. Create a UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // 4. Set up address to bind to
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(listenPort);
    // Listen on any network interface (local machine). If you want a specific IP, use inet_pton, etc.
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // 5. Bind the socket to our specified IP and port
    if (bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    // 6. Wait for a message from the client
    printf("Server listening on port %d...\n", listenPort);

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    char buffer[256];
    memset(buffer, 0, sizeof(buffer));

    // 7. Receive one message (blocking call)
    ssize_t recvBytes = recvfrom(sockfd,
                                 buffer,
                                 sizeof(buffer)-1,  // leave space for '\0'
                                 0,
                                 (struct sockaddr*)&clientAddr,
                                 &clientAddrLen);
    if (recvBytes < 0) {
        perror("recvfrom");
        close(sockfd);
        return 1;
    }
    // Null-terminate the message
    buffer[recvBytes] = '\0';

    // 8. Check if the message is "ftp"
    printf("Received message from client: '%s'\n", buffer);

    if (strcmp(buffer, "ftp") == 0) {
        // 9. If "ftp", respond "yes"
        const char *resp = "yes";
        sendto(sockfd, resp, strlen(resp), 0,
               (struct sockaddr*)&clientAddr, clientAddrLen);
        printf("Sent response: 'yes'\n");
    } else {
        // Else respond "no"
        const char *resp = "no";
        sendto(sockfd, resp, strlen(resp), 0,
               (struct sockaddr*)&clientAddr, clientAddrLen);
        printf("Sent response: 'no'\n");
    }

    // 10. Close the socket
    close(sockfd);
    return 0;
}
