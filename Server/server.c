/*
 * This application is a Unix domain socket server that allows clients to:
 *  - Retrieve a list of files from the server
 *  - Download files from the server
 *  - Upload files to the server
 *  - Communicate with multiple clients concurrently
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dirent.h>

#define SOCKET_PATH "/tmp/unix_socket"  // Path to the Unix domain socket
#define PASSWORD "secret"               // Password required for client authentication
#define FILES_DIR "./files"             // Directory where files are stored

size_t file_data_size = 0;              // Size of the file data received from the client
char *file_data = NULL;                 // Buffer to hold the file data
pthread_mutex_t file_data_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex for synchronizing access to file data

/**
 * Sends the list of files available in the server's directory to the client.
 * The list includes only regular files (not directories).
 */
void send_file_list(int client_sock) {
    DIR *d;
    struct dirent *dir;
    char buffer[1024];

    // Open the directory containing the files
    d = opendir(FILES_DIR);
    if (d) {
        strcpy(buffer, "\nFiles in the directory:\n\n");
        write(client_sock, buffer, strlen(buffer));  // Send header to the client
        
        // Iterate through the directory entries
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {  // Check if the entry is a regular file
                snprintf(buffer, sizeof(buffer), "%s\n", dir->d_name);
                write(client_sock, buffer, strlen(buffer));  // Send the file name to the client
            }
        }
        closedir(d);
    } else {
        // Failed to open the directory, notify the client
        strcpy(buffer, "Unable to open directory.\n");
        write(client_sock, buffer, strlen(buffer));
    }
}

/**
 * Receives a file from the client and saves it to the server's directory.
 */
void receive_file(int client_sock, const char *filename) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", FILES_DIR, filename);  // Construct the full file path

    pthread_mutex_lock(&file_data_mutex);  // Lock the mutex to ensure exclusive access to file data
    
    if (file_data_size > 0) {  // Check if there is any data to write
        FILE *file = fopen(filepath, "w");
        if (file) {
            fwrite(file_data, 1, file_data_size, file);  // Write the received data to the file
            fclose(file);
            printf("File %s downloaded successfully.\n", filename);
            snprintf(filepath, sizeof(filepath), "File %s uploaded successfully.\n", filename);
            write(client_sock, filepath, strlen(filepath));  // Notify the client of successful upload
        } else {
            // Failed to open the file for writing, notify the client
            printf("Error opening file %s for writing.\n", filename);
            strcpy(filepath, "Error writing file to the server.\n");
            write(client_sock, filepath, strlen(filepath));
        }
        // Clean up memory after writing the file
        free(file_data);
        file_data = NULL;
        file_data_size = 0;
    } else {
        // No data was received, notify the client
        printf("No data received from client.\n");
        strcpy(filepath, "No data received for the file.\n");
        write(client_sock, filepath, strlen(filepath));
    }
    
    pthread_mutex_unlock(&file_data_mutex);  // Unlock the mutex
}

/**
 * Sends the requested file to the client.
 */
void send_file(int client_sock, const char *filename) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", FILES_DIR, filename);  // Construct the full file path
    
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        // File not found, notify the client
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "File %s not found.\n", filename);
        write(client_sock, buffer, strlen(buffer));
        return;
    }
    
    // Prepare a buffer to read file data into
    char buffer[1024];
    strcpy(buffer, "||");  // Prefix indicating file data follows
    
    size_t bytes_read = fread(buffer + 2, 1, sizeof(buffer) - 2, file);  // Read the file data
    write(client_sock, buffer, bytes_read + 2);  // Send the data to the client
    sleep(1);  // Short delay to ensure the data is sent before closing the file
    
    fclose(file);
    
    // Notify the client of successful download
    snprintf(buffer, sizeof(buffer), "File %s downloaded successfully.\n", filename);
    write(client_sock, buffer, strlen(buffer));
}

/**
 * Handles communication with a connected client.
 * This function runs in a separate thread for each client.
 */
void *handle_client(void *arg) {
    int client_sock = *(int *)arg;
    char buffer[256];
    int readFile = 0;  // Flag to indicate if we're expecting to receive a file
    char name[256];    // Buffer to hold the name of the file being uploaded
    free(arg); 

    printf("Client connected.\n");

    // Authenticate the client by asking for a password
    while (1) {
        strcpy(buffer, "Enter the password: ");
        write(client_sock, buffer, strlen(buffer));  // Prompt for the password

        memset(buffer, 0, sizeof(buffer));
        int n = read(client_sock, buffer, sizeof(buffer));  // Read the password input
        if (n <= 0) {
            break;  // Client disconnected
        }

        buffer[strcspn(buffer, "\n")] = 0;  // Remove the newline character

        if (strcmp(buffer, PASSWORD) == 0) {  // Check if the password is correct
            strcpy(buffer, "Password correct.\n");
            write(client_sock, buffer, strlen(buffer));
            break;  // Proceed with the session
        } else {
            // Incorrect password, ask again
            strcpy(buffer, "Password incorrect. Try again.\n");
            write(client_sock, buffer, strlen(buffer));
        }
    }

    // Main communication loop with the client
    while (1) {
        if (!readFile) {  // Only prompt for a command if not in the middle of receiving a file
            memset(buffer, 0, sizeof(buffer));
            strcpy(buffer, "Enter the command (list, download <filename>, upload <filename>, exit):\n");
            sleep(1);  // Short delay to ensure prompt is fully sent
            write(client_sock, buffer, strlen(buffer));  // Send the command prompt
        }
        
        memset(buffer, 0, sizeof(buffer));
        int n = read(client_sock, buffer, sizeof(buffer));  // Read the client's command
        if (n <= 0) {
            break;  // Client disconnected
        }

        buffer[strcspn(buffer, "\n")] = 0;  // Remove the newline character

        if (strncmp(buffer, "||", 2) == 0) {  // Check if the received data is part of a file upload
            pthread_mutex_lock(&file_data_mutex);  // Lock the mutex to handle file data
            file_data = realloc(file_data, n - 2);
            memcpy(file_data, buffer + 2, n - 2);  // Store the file data
            file_data_size = n - 2;
            pthread_mutex_unlock(&file_data_mutex);  // Unlock the mutex
        }

        if (strcmp(buffer, "list") == 0) {
            send_file_list(client_sock);  // Send the list of files
        } else if (strncmp(buffer, "download ", 9) == 0) {
            send_file(client_sock, buffer + 9);  // Send the requested file
        } else if (strncmp(buffer, "upload ", 7) == 0) {
            readFile = 1;  // Expect file data in the next message
            strncpy(name, buffer + 7, sizeof(name));  // Extract the file name
            name[sizeof(name) - 1] = '\0';  // Ensure null termination
        } else if (strcmp(buffer, "exit") == 0) {
            printf("Client disconnected.\n");
            break;  // Exit the loop and close the connection
        }
        
        if (readFile && file_data_size > 0) {  // Check if file data has been received
            receive_file(client_sock, name);  // Save the received file
            readFile = 0; 
        }
        
        printf("client sent: %s\n", buffer);  // Log the client's message
    }

    close(client_sock);  // Close the client socket
    pthread_exit(NULL);  // Terminate the thread
}

/**
 * Entry point of the server application.
 * Sets up the Unix domain socket and handles incoming client connections.
 */
int main() {
    int server_sock, *client_sock;
    struct sockaddr_un server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Create a Unix domain socket
    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");  // Print an error message if socket creation fails
        exit(EXIT_FAILURE);
    }

    // Initialize the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    // Unlink the socket path in case it already exists to avoid conflicts
    unlink(SOCKET_PATH);
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");  // Print an error message if binding fails
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_sock, 5) < 0) {
        perror("listen");  // Print an error message if listening fails
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on %s\n", SOCKET_PATH);  // Log the server's status

    // Main loop: accept and handle incoming client connections
    while (1) {
        client_sock = malloc(sizeof(int));  // Allocate memory for the client socket descriptor
        *client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (*client_sock < 0) {
            perror("accept");  // Print an error message if accepting a connection fails
            free(client_sock);  // Free the allocated memory
            continue;
        }

        // Create a new thread to handle the client connection
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, (void *)client_sock);
        pthread_detach(thread_id);  // Detach the thread so it cleans up after itself
    }

    // Clean up: close the server socket and remove the socket file
    close(server_sock);
    unlink(SOCKET_PATH);

    return 0;
}
