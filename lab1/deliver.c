#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int main(int argc, char* argv[]){ // argc is the # of args, argv are the actual args strings
    // Check number of arguments passed in the command-line
    if (argc < 3) {
        fprintf(stderr, "Command must be in the following format: %s <server IP> <server Port>\n", argv[0]);
        return 1;
    }

    // Parse inputs
    const char* serverIp = argv[1];
    int serverPort = atoi(argv[2]); // from string to int

    // Create udp socket
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0); // af_inet is ipv4, sock_dgram is udp, 0 is std protocol
    if(socket_fd < 0){ // -1 return indicates the creation of the socket was not successful
        perror("socket");
        return 1;
    }

    // Set up server address structure
    // Tells the socket where to send the data
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr)); // clean garbage
    serverAddr.sin_family = AF_INET; // indicate it's ipv4
    serverAddr.sin_port = htons(serverPort); // htons are used to convert to big-endian (used in networking protocols)

    // Convert the server IP (string) to a network address
    if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) <= 0) { // parsing ip address to its binary form
        perror("inet_pton");
        close(socket_fd);
        return 1;
    }

    // Ask for input command
    printf("Please enter your command in the format: ftp <filename>\n");
    char userInput[256], command[8], fileName[128];
    if(!fgets(userInput, sizeof(userInput), stdin)){
        fprintf(stderr, "Error reading input.\n");
        close(socket_fd);
        return 1;
    }

    // Check if input is in the right format and extract command and fileName
    if (sscanf(userInput, "%s %s", command, fileName) != 2) {
        fprintf(stderr, "Invalid input. Expected: ftp <filename>\n");
        close(socket_fd);
        return 1;
    }

    // Verify command
    if (strcmp(command, "ftp") != 0) {
        fprintf(stderr, "Invalid command. Please use 'ftp <filename>'.\n");
        close(socket_fd);
        return 1;
    }

    // Check if file exists
    FILE *fp = fopen(fileName, "rb");
    if (!fp) {
        perror("fopen");
        close(socket_fd);
        return 1;
    }
    fclose(fp);

    // Send message to server
    const char* message = "ftp";
    ssize_t bytesSent = sendto(socket_fd, message, strlen(message), 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (bytesSent < 0) {
        perror("sendto");
        close(socket_fd);
        return 1;
    }

    // Receive response
    char buffer[256];
    memset(buffer, 0, sizeof(buffer)); // clear garbage data in array
    socklen_t addrLen = sizeof(serverAddr);

    ssize_t bytesRecv = recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&serverAddr, &addrLen);
    if (bytesRecv < 0) {
        perror("recvfrom");
        close(socket_fd);
        return 1;
    }
    buffer[bytesRecv] = '\0'; // Null terminate

    // Check server response
    if (strcmp(buffer, "yes") == 0) {
        printf("A file transfer can start.\n");
    } else {
        printf("Response: %s\n", buffer);
    }

    close(socket_fd);
    return 0;
}


// Goal of the lab
// 1. Send a message ("ftp") to a server host
// 2. Receive a response from the server
// 3. Determine if a file transfer may proceed based on that response

// We must use UDP sockets

// Parse input from the command line to get ip address and port number
    // Ip address indicates where the server is located in the network
    // Port number indicates which door the server is listening

// Create udp socket to open the communication channel between the UG machines

// Set up structure of the server address -> struct will hold details like port number and ip address
// in a format accepted by functions like sendto() 


