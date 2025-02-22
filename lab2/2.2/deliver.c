#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>

#define MAX_DATA_SIZE 1000
#define PACKET_BUFFER_SIZE 1500

int send_fragment(int sockfd, FILE *fp, long fileSize, int frag_no, int num_frags,
                  const char *fileName, struct sockaddr_in *serverAddr);

int main(int argc, char *argv[]) { // argc is the # of args, argv are the actual args strings
    // Check number of arguments passed in the command-line
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server IP> <server Port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Parse inputs
    const char *serverIp = argv[1];
    int serverPort = atoi(argv[2]); // from string to int

    // Create udp socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0); // af_inet is ipv4, sock_dgram is udp, 0 is std protocol
    if (sockfd < 0) { // -1 return indicates the creation of the socket was not successful
        perror("socket");
        return EXIT_FAILURE;
    }

    // Set up server address structure
    // Tells the socket where to send the data
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr)); // clean garbage
    serverAddr.sin_family = AF_INET; // indicate it's ipv4
    serverAddr.sin_port = htons(serverPort); // htons are used to convert to big-endian (used in networking protocols)

    if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) <= 0) { // converts IP Address to binary
        perror("inet_pton");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Ask for input command
    printf("Please enter your command in the format: ftp <filename>\n");
    char userInput[256], command[8], fileName[128];
    if (!fgets(userInput, sizeof(userInput), stdin)) {
        fprintf(stderr, "Error reading input.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Check if input is in the right format and extract command and fileName
    if (sscanf(userInput, "%7s %127s", command, fileName) != 2) {
        fprintf(stderr, "Invalid input. Expected: ftp <filename>\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Verify command
    if (strcmp(command, "ftp") != 0) {
        fprintf(stderr, "Invalid command. Please use 'ftp <filename>'.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Check if file exists
    FILE *fp = fopen(fileName, "rb");
    if (!fp) {
        perror("fopen");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // determine file size
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp); // get file size

    rewind(fp); // reset pointer

    int num_frags;
    if(fileSize == 0){
        num_frags = 1; // if file is empty send one fragment with size = 0
    }
    else{
        num_frags = (int)((fileSize + MAX_DATA_SIZE - 1) / MAX_DATA_SIZE);
    }

    printf("File size: %ld bytes\n", fileSize);
    printf("Number of fragments: %u\n", num_frags);

    // Start timer
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Read and send packets
    for (int i = 1; i <= num_frags; i++) { 
        if (send_fragment(sockfd, fp, fileSize, i, num_frags, fileName, &serverAddr) != 0) {
            fclose(fp);
            close(sockfd);
            return EXIT_FAILURE;
        }
    }  

    printf("File transfer completed.\n");

    // End timer and measure
    clock_gettime(CLOCK_MONOTONIC, &end);
    double rtt = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Round-trip time: %.6f seconds\n", rtt);

    fclose(fp);
    close(sockfd);
    return 0;
}


int send_fragment(int sockfd, FILE *fp, long fileSize, int frag_no, int num_frags,
                  const char *fileName, struct sockaddr_in *serverAddr) {
    char packet_buffer[PACKET_BUFFER_SIZE]; // Buffer to store the full packet

    char data[MAX_DATA_SIZE];
    size_t bytesRead = 0;
    if (fileSize > 0) {
        bytesRead = fread(data, 1, MAX_DATA_SIZE, fp); // reads 1000 bytes from fp and stores in data
        printf("Bytes read %zu\n", bytesRead);
    }

    // Create packet header
    int header_len = snprintf(packet_buffer, PACKET_BUFFER_SIZE, "%u:%u:%u:%s:", num_frags, frag_no, (int)bytesRead, fileName);

    // error check
    if (header_len < 0 || header_len >= PACKET_BUFFER_SIZE) {
        return -1;
    }

    if (header_len + bytesRead > PACKET_BUFFER_SIZE) {
        return -1;
    }

    // copy file data into the packet buffer after header
    memcpy(packet_buffer + header_len, data, bytesRead);

    // send the packet
    ssize_t packetSize = header_len + bytesRead;
    ssize_t sentBytes = sendto(sockfd, packet_buffer, packetSize, 0, (struct sockaddr *)serverAddr, sizeof(*serverAddr));

    if (sentBytes < 0) {
        perror("sendto");
        return -1;
    }
    printf("Sent packet %u/%u (header %d bytes, data %zu bytes)\n", frag_no, num_frags, header_len, bytesRead);

    // wait for the ACK
    char ack_buffer[256];
    socklen_t addrLen = sizeof(*serverAddr);
    ssize_t ackBytes = recvfrom(sockfd, ack_buffer, sizeof(ack_buffer) - 1, 0,
                                (struct sockaddr *)serverAddr, &addrLen);
    if (ackBytes < 0) {
        perror("recvfrom");
        return -1;
    }
    ack_buffer[ackBytes] = '\0'; 

    if (strcmp(ack_buffer, "received") == 0) {
        printf("ACK received for packet %u/%u\n", frag_no, num_frags);
    } else {
        fprintf(stderr, "Unexpected ACK response: \"%s\"\n", ack_buffer);
        return -1;
    }

    return 0; // Success
}
