#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int main(int argc, char* argv[]){ // argc is the # of args, argv are the actual args strings
    // 1. Check usage
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server IP> <server Port>\n", argv[0]);
        return 1;
    }

    // 2. Parse inputs
    const char* serverIp = argv[1];
    int serverPort = atoi(argv[2]); // from string to int

    // 3. Create udp socket
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0); // af_init is ipv4, sock_dgram is udp, 0 is std protocol
    if(socket_fd < 0){
        perror("socket");
        return 1;
    }

    // 4. Set up server address structure
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr)); // clean garbage
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);

    // Convert the server IP (string) to a network address
    if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(socket_fd);
        return 1;
    }

    // 5. Prompt for command
    printf("Please enter your command in the format: ftp <filename>\n");
    char userInput[256], command[16], fileName[128];
    if(!fgets(userInput, sizeof(userInput), stdin)){
        fprintf(stderr, "Error reading input.\n");
        close(socket_fd);
        return 1;
    }

    // 6. Parse user input into "command" and "fileName"
    if (sscanf(userInput, "%s %s", command, fileName) != 2) {
        fprintf(stderr, "Invalid input. Expected: ftp <filename>\n");
        close(socket_fd);
        return 1;
    }

    // 7. Verify command is "ftp"
    if (strcmp(command, "ftp") != 0) {
        fprintf(stderr, "Invalid command. Please use 'ftp <filename>'.\n");
        close(socket_fd);
        return 1;
    }

    // 8. Check if file exists locally
    FILE *fp = fopen(fileName, "rb");
    if (!fp) {
        perror("fopen");
        close(socket_fd);
        return 1;
    }
    fclose(fp);

    // 9. Send "ftp" to the server
    const char* message = "ftp";
    ssize_t bytesSent = sendto(socket_fd, message, strlen(message), 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (bytesSent < 0) {
        perror("sendto");
        close(socket_fd);
        return 1;
    }

    // 10. Receive server response
    char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    socklen_t addrLen = sizeof(serverAddr);

    ssize_t bytesRecv = recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&serverAddr, &addrLen);
    if (bytesRecv < 0) {
        perror("recvfrom");
        close(socket_fd);
        return 1;
    }
    buffer[bytesRecv] = '\0'; // Null terminate

    // 11. Check server response
    if (strcmp(buffer, "yes") == 0) {
        printf("A file transfer can start.\n");
    } else {
        printf("Server responded: %s\n", buffer);
        printf("Exiting.\n");
    }

    // 12. Cleanup
    close(socket_fd);
    return 0;
}
