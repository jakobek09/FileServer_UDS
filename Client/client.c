/*
 * This application is a client for a Unix domain socket server.
 * It allows the user to:
 *  - Authenticate with the server using a password
 *  - Download files from the server
 *  - Upload files to the server
 *  - Send commands to the server (list, download, upload, exit)
 *  - Receive and display messages from the server
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/unix_socket"  // Path to the Unix domain socket

int sock;  // Socket descriptor for communication with the server
char buffer[1024];  // Buffer for receiving data from the server
char *line = NULL;  // Buffer for user input
size_t len = 0;  // Length of the user input buffer
char *file_data = NULL;  // Buffer to store file data received from the server
size_t file_data_size = 0;  // Size of the file data received
pthread_mutex_t file_data_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex for synchronizing access to file data
int passwordAccepted = 0;  // Flag indicating if the password has been accepted by the server

/**
 * Thread function to continuously read messages from the server.
 * It handles incoming data, including file data prefixed with "||".
 */
void *read_from_server(void *arg) {
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = read(sock, buffer, sizeof(buffer));  // Read data from the server
        if (n <= 0) {
            // Server has disconnected, exit the thread
            printf("Server has disconnected.\n");
            close(sock);
            pthread_exit(NULL);
        }

        // Check if the password was accepted
        if (strstr(buffer, "Password correct."))
            passwordAccepted = 1;

        // Handle incoming file data
        if (strncmp(buffer, "||", 2) == 0) {
            pthread_mutex_lock(&file_data_mutex);  // Lock the mutex to modify file data
            file_data = realloc(file_data, n - 2);  // Allocate memory for file data
            memcpy(file_data, buffer + 2, n - 2);  // Copy the file data into the buffer
            file_data_size = n - 2;
            pthread_mutex_unlock(&file_data_mutex);  // Unlock the mutex
        } else {
            // Print any other message received from the server
            printf("%s", buffer);
        }
        
        fflush(stdout);  // Ensure all output is flushed
    }
    pthread_exit(NULL);  // Exit the thread
}

/**
 * Downloads a file by writing the received data to a file on the client side.
 */
void download_file(const char *filename) {
    pthread_mutex_lock(&file_data_mutex);  // Lock the mutex to safely access file data
    
    if (file_data_size > 0) {
        // Open the file for writing
        FILE *file = fopen(filename, "w");
        if (file) {
            fwrite(file_data, 1, file_data_size, file);  // Write the file data to disk
            fclose(file);  // Close the file
            //printf("%s downloaded successfully.\n", filename);
        } else {
            printf("Error opening %s for writing.\n", filename);
        }
        // Clean up the file data buffer
        free(file_data);
        file_data = NULL;
        file_data_size = 0;
    } else {
        // No data was received from the server
        printf("No data received from server.\n");
    }
    
    pthread_mutex_unlock(&file_data_mutex);  // Unlock the mutex
}

/**
 * Uploads a file to the server by reading the file and sending its content over the socket.
 */
void send_file(int sock, const char *filename) {

    // Open the file for reading
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        // If the file does not exist, notify the server
        char bufferr[256];
        snprintf(bufferr, sizeof(bufferr), "File %s not found.\n", filename);
        printf("File not found!\n");
        write(sock, bufferr, strlen(bufferr));
        return;
    }

    // Get the file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate buffer for file content plus the prefix
    char *buffer2 = malloc(file_size + 2);
    if (buffer2 == NULL) {
        // Handle memory allocation failure
        perror("malloc");
        fclose(file);
        return;
    }

    // Add the prefix "||" to indicate the start of file data
    strcpy(buffer2, "||");

    // Read the file content into the buffer
    size_t bytes_read = fread(buffer2 + 2, 1, file_size, file);
    if (bytes_read != file_size) {
        perror("fread");
        free(buffer2);
        fclose(file);
        return;
    }

    // Send the buffer to the server
    if (write(sock, buffer2, bytes_read + 2) < 0) {
        perror("write");
    }

    fflush(stdout); // Ensure all output is flushed
    free(buffer2); // Free the allocated buffer
    fclose(file); // Close the file
}

int main() {
    struct sockaddr_un server_addr;

    // Create a Unix domain socket
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");  // Handle socket creation failure
        exit(EXIT_FAILURE);
    }

    // Initialize the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");  // Handle connection failure
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Create a thread to handle reading from the server
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, read_from_server, NULL);

    // Loop to handle password input until it is accepted
    while (1) {
        getline(&line, &len, stdin);  // Read user input
        write(sock, line, strlen(line));  // Send the password to the server
        sleep(1);  // Wait for the server response
        if (passwordAccepted) {
            break;  // Exit the loop if the password is correct
        }
    }

    // Main loop to handle user commands
    while (1) {
        getline(&line, &len, stdin);  // Read user input
        write(sock, line, strlen(line));  // Send the command to the server

        line[strcspn(line, "\n")] = 0;  // Remove the newline character
        
        if (strncmp(line, "download ", 9) == 0) {
            // Handle file download
            sleep(1);  // Wait for the server to send the file data
            download_file(line + 9);  // Download the file
        } else if (strncmp(line, "upload ", 7) == 0) {
            // Handle file upload
            sleep(3);  // Wait before starting the file upload
            send_file(sock, line + 7);  // Upload the file
        } else if (strncmp(line, "exit", 4) == 0) {
            break;  // Exit the loop and close the connection
        } else if (strncmp(line, "list", 4) == 0) {
            // Handle listing files on the server
        } else {
            printf("Unknown command\n");  // Handle unknown commands
        }
        fflush(stdout);  // Ensure all output is flushed
    }

    // Clean up resources
    free(line);
    close(sock);

    return 0;
}
