#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>



#define MAX_NEIGHBORS 17
#define PEERPORT 7005
#define SERVER_PORT 7002
#define PEERIP "127.0.0.1"
#define SERVER_IP "127.0.0.1"
#define BACKLOG 100
#define MAX_MESSAGE_ID_LENGTH 128
#define MAX_ENTRIES 102
#define MAX_BUFFER_SIZE 1024

//Message info
struct Message {
    char message_id[MAX_MESSAGE_ID_LENGTH];
    int ttl;
    char filename[50];
};

// Message ID info
struct MessageIDInfo {
    char message_id[MAX_MESSAGE_ID_LENGTH];
    char upstream_peer[INET6_ADDRSTRLEN];
    int port;
};

//Hit message info
struct Hit {
    char message_id[MAX_MESSAGE_ID_LENGTH];
    int ttl;
    char filename[50];
    char peer_ip[16];
    int peer_port;
};

struct ConnectionArgs {
    int new_client_socket;
    const char* client_ip;
};

// Peer info
struct Peer {
    char id[32];
    char ip[16];
    int port;
};

struct MyThreadArgs {
    struct Hit hit;
    char peer_ip[16];
    int port;
};

struct ThreadArgs {
    struct Message received_message;
    struct Peer neighbor;
};

struct MessageSentID {
    char message_id[MAX_MESSAGE_ID_LENGTH];
};

//Creating array to store the message info
struct MessageIDInfo messageIDTable[MAX_ENTRIES];
int messageIDTableSize = 0;
int num_neighbors;
int message_count = 0;
struct MessageSentID messagesentID[MAX_ENTRIES];
pthread_mutex_t messageIDTableLock = PTHREAD_MUTEX_INITIALIZER;


//helping functions
//receive functions
int safe_recv(int tsock,void *buf,uint32_t bytes,int flags) {
        int num_bytes;
        uint32_t read = bytes;

        while (read > 0) {
            if ((num_bytes = recv(tsock,buf,read,flags)) <= 0) {
                    perror("SAFE RECV");
                    // we use normal exit here because  this will be called on the client
                    exit(1);

            }
            buf += num_bytes;
            read -= num_bytes;
        }
        return 0;
}
//send functions
int safe_send(int tsock,const void *msg,uint32_t len,int flags) {
        int num_bytes;
        // byte pointer
        char *buf = (char *)msg;
        uint32_t bytes_left = len;

        while (bytes_left > 0) {
                if ((num_bytes = send(tsock,buf,bytes_left,flags)) <= 0) {
                        perror("SAFE_SEND");
                        exit(1);
                }
                buf += num_bytes;
                bytes_left -= num_bytes;

        }
        return 0;
}

void serializeHit(const struct Hit *hit, char *buffer) {
    // Serialize message_id, ttl, filename, peer_ip, peer_port
    memcpy(buffer, hit->message_id, MAX_MESSAGE_ID_LENGTH);
    buffer += MAX_MESSAGE_ID_LENGTH;

    int ttl = htonl(hit->ttl);
    memcpy(buffer, &ttl, sizeof(int));
    buffer += sizeof(int);

    strcpy(buffer, hit->filename);
    buffer += strlen(hit->filename) + 1;

    strcpy(buffer, hit->peer_ip);
    buffer += strlen(hit->peer_ip) + 1;

    int peer_port = htonl(hit->peer_port);
    memcpy(buffer, &peer_port, sizeof(int));
}

void deserializeHit(const char *buffer, struct Hit *hit) {
    // Deserialize message_id, ttl, filename, peer_ip, peer_port
    memcpy(hit->message_id, buffer, MAX_MESSAGE_ID_LENGTH);
    buffer += MAX_MESSAGE_ID_LENGTH;

    int ttl;
    memcpy(&ttl, buffer, sizeof(int));
    hit->ttl = ntohl(ttl);
    buffer += sizeof(int);

    strcpy(hit->filename, buffer);
    buffer += strlen(buffer) + 1;

    strcpy(hit->peer_ip, buffer);
    buffer += strlen(buffer) + 1;

    int peer_port;
    memcpy(&peer_port, buffer, sizeof(int));
    hit->peer_port = ntohl(peer_port);
}

void serializeMessage(const struct Message *message, char *buffer) {
    // Serialize message_id, ttl, filename
    memcpy(buffer, message->message_id, MAX_MESSAGE_ID_LENGTH);
    buffer += MAX_MESSAGE_ID_LENGTH;

    int ttl = htonl(message->ttl);
    memcpy(buffer, &ttl, sizeof(int));
    buffer += sizeof(int);

    strcpy(buffer, message->filename);
    buffer += strlen(message->filename) + 1;
}

void deserializeMessage(const char *buffer, struct Message *message) {
    // Deserialize message_id, ttl, filename
    memcpy(message->message_id, buffer, MAX_MESSAGE_ID_LENGTH);
    buffer += MAX_MESSAGE_ID_LENGTH;

    int ttl;
    memcpy(&ttl, buffer, sizeof(int));
    message->ttl = ntohl(ttl);
    buffer += sizeof(int);

    strcpy(message->filename, buffer);
    buffer += strlen(buffer) + 1;
}

// Parse config file and initialize neighbors
int init_peers(char* config_file, struct Peer* neighbors[]) {

    FILE* fp = fopen(config_file, "r");

    if (!fp) {
        printf("Error opening config file\n");
        return -1;
    }

    char line[128];
    int num_peers = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (num_peers >= MAX_NEIGHBORS) {
            break;
        }

        // Parse peer id, IP, port from config line
        char id[32], ip[16];
        int port;

        sscanf(line, "%s %s %d", id, ip, &port);


        // Allocate memory for the new neighbor
        neighbors[num_peers] = (struct Peer*)malloc(sizeof(struct Peer));
        if (!neighbors[num_peers]) {
            printf("Error allocating memory for neighbor %d\n", num_peers);
            fclose(fp);
            return -1;
        }


        // Copy parsed data into the neighbor structure
        strncpy(neighbors[num_peers]->id, id, sizeof(neighbors[num_peers]->id) - 1);
        strncpy(neighbors[num_peers]->ip, ip, sizeof(neighbors[num_peers]->ip) - 1);
        neighbors[num_peers]->port = port;
        num_peers++;
    }

    fclose(fp);
    return num_peers;
}

int findPort(struct Peer* neighbors[], int num_neighbors, const char* client_ip) {
    for (int i = 0; i < num_neighbors; i++) {
        if (strcmp(neighbors[i]->ip, client_ip) == 0) {
            // IP address matches, return the port number
            return neighbors[i]->port;
        }
    }
}

void printPeers(struct Peer* neighbors[], int num_neighbors) {
    printf("Peers:\n");
    for (int i = 0; i < num_neighbors; i++) {
        printf("Peer %d:\n", i + 1);
        printf("ID: %s\n", neighbors[i]->id);
        printf("IP: %s\n", neighbors[i]->ip);
        printf("Port: %d\n", neighbors[i]->port);
        printf("\n");
    }
}


//Searching the file locally
void searchFiles(const char *dirPath, const char *file_name, const char *message_id) {
    DIR *dir = opendir(dirPath);
    if (!dir) {
        perror("Error opening directory");
        return;
    }

    struct dirent *entry;
    char subDirPath[256];
    int result; // To store the result of snprintf

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                // Safely concatenate directory path and subdirectory name
                result = snprintf(subDirPath, sizeof(subDirPath), "%s/%s", dirPath, entry->d_name);

                if (result >= sizeof(subDirPath)) {
                    // Handle the case where the concatenated string is too long
                    fprintf(stderr, "Directory path is too long: %s/%s\n", dirPath, entry->d_name);
                } else {
                    // Recursively search subdirectories
                    searchFiles(subDirPath, file_name, message_id);
                }
            }
        } else if (entry->d_type == DT_REG) {
            if (strstr(entry->d_name, file_name) != NULL) {
                printf("Found file: %s/%s\n", dirPath, entry->d_name);
                printf("Initiating hit query function \n");

                char peer_ip[16];
                int peer_port;
                for (int i=0; i < messageIDTableSize; i++ ) {
                    if(strcmp(messageIDTable[i].message_id, message_id) ==0) {
                        strcpy(peer_ip, messageIDTable[i].upstream_peer);
                        peer_port = messageIDTable[i].port;
                    }
                }

                int dest_socket = socket(AF_INET, SOCK_STREAM, 0);
                if (dest_socket < 0) {
                    perror("Error in socket creation");
                    exit(1);
                }
                struct sockaddr_in dest_addr;
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(peer_port);
                inet_pton(AF_INET, peer_ip, &(dest_addr.sin_addr));

                // Connect to the peer
                if (connect(dest_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
                    perror("Error in connecting to the peer");
                    close(dest_socket);
                    exit(1);
                }
                // Send the "hit" text message first
                send(dest_socket,"hit",sizeof("hit"),0);
                struct Hit hit;
                strncpy(hit.message_id, message_id, MAX_MESSAGE_ID_LENGTH);
                hit.ttl = 5;
                strncpy(hit.filename, file_name, sizeof(hit.filename));
                strncpy(hit.peer_ip, "10.135.49.182", sizeof("10.135.49.182")); // Insert the sender's IP here
                hit.peer_port = SERVER_PORT; // Insert the sender's port here

                // Send the "Hit" message to the peer
                uint32_t len = sizeof(struct Hit);
                printf("Sending HIt Message ID: %s\n", hit.message_id);
                printf("Sending HIt TTL: %d\n", hit.ttl);
                printf("Sending HIt Filename: %s\n", hit.filename);
                printf("Sending Hit Ip Address: %s\n",hit.peer_ip);
                printf("Sending Hit Port : %d \n ",hit.peer_port);
                char buffer[sizeof(struct Hit)];

                // Serialize the hit struct into the buffer
                serializeHit(&hit, buffer);

                // Send the serialized data to the server
                send(dest_socket, buffer, sizeof(struct Hit), 0);
                // Send the "Hit" struct
                //send(dest_socket, &hit, len, 0);

                // Close the socket when done
                close(dest_socket);

            }
        }
    }
    closedir(dir);
}

// Function to send the "Hit" message
void* sendHitMessage(void* args) {
    struct MyThreadArgs* thread_args = (struct MyThreadArgs*)args;

    // Access the values from the structure
    struct Hit hit = thread_args->hit;
    char* peer_ip = thread_args->peer_ip;
    int peer_port = thread_args->port;
    // Create a socket to connect to the peer
    int dest_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (dest_socket < 0) {
        perror("Error in socket creation");
        exit(1);
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(peer_port);
    inet_pton(AF_INET, peer_ip, &(dest_addr.sin_addr));

    // Connect to the peer
    if (connect(dest_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        perror("Error in connecting to the peer");
        close(dest_socket);
        exit(1);
    }
    printf("%s IP \n",hit.peer_ip);
    printf("%d PORT\n",hit.peer_port);
    printf("%s FILENAME\n",hit.filename);
    printf("%d TTL \n",hit.ttl);
    // Send the "Hit" message to the peer
    //uint32_t len = sizeof(struct Hit);
    // Send the "hit" text message first
    send(dest_socket, "hit", sizeof("hit"), 0);
    // Send the "Hit" struct
    char buffer2[sizeof(struct Hit)];
    struct Hit sendhit = hit;
    printf("%s IP \n",sendhit.peer_ip);
    printf("%d PORT\n",sendhit.peer_port);
    printf("%s FILENAME\n",sendhit.filename);
    printf("%d TTL \n",sendhit.ttl);

    // Serialize the hit struct into the buffer
    serializeHit(&sendhit, buffer2);

    // Send the serialized data to the server
    send(dest_socket, buffer2, sizeof(struct Hit), 0);
    //send(dest_socket, &hit, len,0);

    // Close the socket when done
    pthread_exit(NULL);
    close(dest_socket);
}

// Function to recieve file from peer function
void receiveFileFromPeer(struct Hit hit) {
    int server_socket;
    struct sockaddr_in server_addr;

    // Create a socket to connect to the server
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Error in socket creation");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(hit.peer_port);
    inet_pton(AF_INET, hit.peer_ip, &(server_addr.sin_addr));

    // Connect to the server
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error in connecting to the server");
        exit(1);
    }

    printf("Connected to the Client server.\n");

    send(server_socket,"file",sizeof("file"),0);

    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_received;
    int file_fd;

    // Receive the filename from the peer
    char filename[50];
    strcpy(filename, hit.filename);
    send(server_socket, filename, sizeof(filename),0);

    // Attempt to create the file for writing
    file_fd = open(filename, O_WRONLY | O_CREAT, 0644);
    if (file_fd == -1)
    {
        perror("open");
        return;
    }

    printf("Receiving file '%s' from the peer...\n", filename);

    // Receive and write the file in chunks
    while ((bytes_received = recv(server_socket, buffer, sizeof(buffer), 0)) > 0)
    {
        ssize_t bytes_written = write(file_fd, buffer, (size_t)bytes_received);
        if (bytes_written == -1)
        {
            perror("write");
            break;
        }
    }

    // Close the file
    close(file_fd);

    if (bytes_received == -1)
    {
        perror("recv");
        return;
    }

    printf("File Received\n");
    close(server_socket);
}

// Function to send the file peer function
void sendFileToPeer(int client_socket, const char *filename) {
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read, bytes_sent;
    int file_fd;

    // Attempt to open the requested file
    file_fd = open(filename, O_RDONLY);
    if (file_fd == -1)
    {
        perror("open");
        return;
    }

    printf("Sending file '%s' to the client...\n", filename);

    // Read and send the file in chunks
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0)
    {
        bytes_sent = send(client_socket, buffer, (size_t)bytes_read, 0);
        if (bytes_sent == -1)
        {
            perror("send");
            break;
        }
    }
    printf("File Sent ");

    // Close the file
    close(file_fd);
    return;
}

//forward message to the all his neighbours
void *forward_message(void *arg) {
    struct ThreadArgs *thread_args = (struct ThreadArgs *)arg;
    struct Message message = thread_args->received_message;
    struct Peer server_peer = thread_args->neighbor;
    int server_socket;
    struct sockaddr_in server_addr;

    // Create a socket to connect to the server
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error in socket creation");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_peer.port);
    inet_pton(AF_INET, server_peer.ip, &(server_addr.sin_addr));

    // Connect to the server
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error in connecting to the server");
        exit(1);
    }

    printf("Connected to the server.\n");
    send(server_socket,"forward",sizeof("forward"),0);
    // Send the received message to the server
    uint32_t len = sizeof(struct Message);
    struct Message send_message = message;
    printf("Sending Message ID: %s\n", send_message.message_id);
    printf("Sending TTL: %d\n", send_message.ttl);
    printf("Sending Filename: %s\n", send_message.filename);

    //send(server_socket, &send_message, len, 0);
    char buffer[sizeof(struct Message)];
    serializeMessage(&send_message,buffer);
    send(server_socket, buffer, len, 0);

    // Close the socket after sending the message
    close(server_socket);
}

int search_meassge_sent_id(struct Hit hit) {
    for ( int i = 0; i < message_count; i++ ) {
        if ( strcmp(messagesentID[i].message_id, hit.message_id) == 0) {
            return 1;
        }

    }
    return 0;
}


void* connection_handler(void* args) {
    struct ConnectionArgs* conn_args = (struct ConnectionArgs*)args;
    int new_client_socket = conn_args->new_client_socket;
    const char* client_ip = conn_args->client_ip;
    struct Peer *neighbors[MAX_NEIGHBORS];

    int num_neighbors2 = init_peers("config.txt", neighbors);
    if (num_neighbors2 < 0) {
        // Handle initialization failure
        perror("File has no neighbours");
    }

    char buf[1024];
    int bytes_received = recv(new_client_socket, buf, sizeof(buf) - 1, 0);
    if (bytes_received == -1) {
        perror("recv");
        close(new_client_socket);
        exit(1);
    }

    buf[bytes_received] = '\0'; // Null-terminate the received data
    printf("Message received is: %s\n", buf);

    // The rest of your code here
        if(strcmp(buf, "hit") == 0) {

            //write the code for forwarding the hit-query message
            struct Hit hit;
            char buffer[sizeof(struct Hit)];

            // Receive the serialized data using safe_recv
            if (safe_recv(new_client_socket, buffer, sizeof(struct Hit), 0) != 0) {
                perror("Error receiving 'struct Hit' from the client");
                exit(1);
            }
            //safe_recv(new_client_socket, buffer, sizeof(buffer), 0);

            // Deserialize the received data into the hit struct
            deserializeHit(buffer, &hit);

            //recv(new_client_socket, &hit, len, 0);
            printf("%s\n",hit.peer_ip);
            printf("%d\n",hit.peer_port);
            if(search_meassge_sent_id(hit)) {
                // write the code to establish the connection and receive the file
                receiveFileFromPeer(hit);
            }
            else {

                if (hit.ttl > 0) {
                // Decrease the TTL by 1 before forwarding
                    hit.ttl--;
                    // Search the local peer table to get the next peer's IP and port
                    char next_peer_ip[16];
                    int next_peer_port ;

                    for (int i=0; i < messageIDTableSize; i++ ) {
                        if(strcmp(messageIDTable[i].message_id, hit.message_id) ==0) {
                            strcpy(next_peer_ip, messageIDTable[i].upstream_peer);
                            next_peer_port = messageIDTable[i].port;
                            printf("%d is nextpeerport, %s is next_peer_ip", messageIDTable[i].port, messageIDTable[i].upstream_peer);
                        }
                    }


                    if (next_peer_ip && next_peer_port) {
                        // Forward the "hit" message to the next peer
                        struct MyThreadArgs thread_args;
                        thread_args.port = next_peer_port;
                        strcpy(thread_args.peer_ip, next_peer_ip);
                        //thread_args.peer_ip = next_peer_ip;
                        thread_args.hit = hit;
                        pthread_t thread;
                        if (pthread_create(&thread, NULL, sendHitMessage, &thread_args) != 0) {
                                perror("Thread creation failed");
                                //return ;
                        }
                        pthread_join(thread, NULL);
                    }
                }
            }
        }
        else if (strcmp(buf, "file") == 0) {
            char filename[50];
            recv(new_client_socket, filename, sizeof(filename),0);
            sendFileToPeer(new_client_socket,filename);
        }

        else {
            //recieving message from the another client
            struct Message received_message;
            char buffer[sizeof(struct Message)];

            if (safe_recv(new_client_socket, buffer, sizeof(struct Message), 0) != 0) {
                perror("Error receiving 'struct Hit' from the client");
                exit(1);
            }
            // Deserialize the received data into the hit struct
            deserializeMessage(buffer, &received_message);

            // Receive the struct Message from the client
            //recv(new_client_socket, &received_message, len, 0);

            // Now, you can access the received_message struct and process the data
            printf("Received Message ID: %s\n", received_message.message_id);
            printf("Received TTL: %d\n", received_message.ttl);
            printf("Received Filename: %s\n", received_message.filename);
            int message_already_seen = 0;
            if(message_count > 0 ) {
                for (int i = 0; i < message_count; i++) {
                    if (strcmp(received_message.message_id, messagesentID[i].message_id) == 0) {
                        message_already_seen = 1;
                        pthread_mutex_unlock(&messageIDTableLock);
                        close(new_client_socket);
                        return NULL;
                        break;

                    }
                }
            }


            // Check if the message ID is seen in the table structure
            // Lock the mutex to protect access to messageIDTable
            //
            pthread_mutex_lock(&messageIDTableLock);

            if(messageIDTableSize > 0 ) {
                for (int i = 0; i < messageIDTableSize; i++) {
                    if (strcmp(received_message.message_id, messageIDTable[i].message_id) == 0) {
                        message_already_seen = 1;
                        pthread_mutex_unlock(&messageIDTableLock);
                        close(new_client_socket);
                        return NULL;
                        break;
                    }
                }
            }

            if (message_already_seen) {
                    printf("Query already recieved");

            }
            else {

                if (messageIDTableSize < MAX_ENTRIES) {
                    strncpy(messageIDTable[messageIDTableSize].message_id, received_message.message_id, sizeof(messageIDTable[messageIDTableSize].message_id));
                    // Store the upstream peer information here

                    strncpy(messageIDTable[messageIDTableSize].upstream_peer, client_ip, sizeof(messageIDTable[messageIDTableSize].upstream_peer));
                    messageIDTable[messageIDTableSize].port = findPort(neighbors, num_neighbors2, client_ip);
                    messageIDTableSize++;
                }
                else {
                    // Implement logic to remove old entries from the table to prevent unbounded growth
                    for (int i = 1; i < messageIDTableSize; i++) {
                        strcpy(messageIDTable[i - 1].message_id, messageIDTable[i].message_id);
                        strcpy(messageIDTable[i - 1].upstream_peer, messageIDTable[i].upstream_peer);
                    }

                    // Add the new entry at the end
                    strncpy(messageIDTable[messageIDTableSize - 1].message_id, received_message.message_id, sizeof(messageIDTable[messageIDTableSize - 1].message_id));
                    strncpy(messageIDTable[messageIDTableSize - 1].upstream_peer, client_ip, sizeof(messageIDTable[messageIDTableSize - 1].upstream_peer));
                    messageIDTable[messageIDTableSize].port = findPort(neighbors, num_neighbors2, client_ip);
                }
                received_message.ttl = received_message.ttl-1;
                printf("Decrementing the TTL: %d before forwarding \n", received_message.ttl);

                // Unlock the mutex after updating messageIDTable
                pthread_mutex_unlock(&messageIDTableLock);

                pthread_t forward_thread[num_neighbors];
                if(received_message.ttl > 0) {
                    for (int i = 0; i < num_neighbors2; i++) {
                        struct ThreadArgs thread_args;
                        thread_args.received_message = received_message;
                        thread_args.neighbor = *neighbors[i];

                        if (pthread_create(&forward_thread[i], NULL, forward_message, &thread_args) != 0) {
                            perror("Error in creating thread 2");
                            exit(1);
                        }
                        pthread_join(forward_thread[i], NULL);
                    }
                }
                //Serch locally
                const char *directoryPath = "../pa2";  // Replace with the path to your directory

                searchFiles(directoryPath, received_message.filename, received_message.message_id);

            }
        }

    // Don't forget to close the new_client_socket when done
    close(new_client_socket);

    return NULL;
}




void *connect_to_server(void *arg) {
    int server_socket;
    struct sockaddr_in server_addr;
    struct Peer *peer = (struct Peer *)arg;

    // Create a socket to connect to the server
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Error in socket creation");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(peer->port);
    inet_pton(AF_INET, peer->ip, &(server_addr.sin_addr));

    // Connect to the server
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error in connecting to the server");
        exit(1);
    }

    printf("Connected to the server.\n");

    send(server_socket, "hey", sizeof("hey"),0 );

    //Sending the message to the server
    struct Message message;
    strncpy(message.message_id, "1002", MAX_MESSAGE_ID_LENGTH);
    message.ttl = 2;
    strncpy(message.filename, "example.txt", sizeof(message.filename));
    char buffer[sizeof(struct Message)];
    serializeMessage(&message,buffer);
    send(server_socket, buffer, sizeof(struct Message), 0);
    // Send the struct Message to the server
    // uint32_t len = sizeof(struct Message);
    // send(server_socket, &message, len, 0);

    strncpy(messagesentID[message_count].message_id, "1001", MAX_MESSAGE_ID_LENGTH);
    message_count++;


}


int create_client_socket() {
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    return client_socket;
}

// Function to bind the client socket
int bind_client_socket(int client_socket) {
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        return -1; // Return an error code
    }

    return 0; // Success
}



int main() {
    struct Peer* neighbors[MAX_NEIGHBORS];
    int num_neighbors = init_peers("config.txt", neighbors);

    if (num_neighbors < 0) {
        // Handle initialization failure
        return 1;
    }

    int client_socket = create_client_socket(); // Function to create the client socket

    if (client_socket < 0) {
        perror("Error in socket creation for server");
        return 1;
    }

    struct sockaddr_in client_addr; // Declare client_addr here

    if (bind_client_socket(client_socket) < 0) { // Function to bind the client socket
        perror("Error in binding client socket");
        return 1;
    }

    if (listen(client_socket, 10) == 0) {
        printf("Listening for other clients...\n");
    } else {
        perror("Error in listening for other clients");
        return 1;
    }
    pthread_t connect_thread[num_neighbors];
    for (int i = 0; i < num_neighbors; i++) {
        if (pthread_create(&connect_thread[i], NULL, connect_to_server, neighbors[i]) != 0)
        {
            perror("Error in creating thread 2");
            exit(1);
        }
    }

    while (1) {
        // Accept incoming connections from other clients
        socklen_t client_addr_len = sizeof(client_addr);
        int new_client_socket = accept(client_socket, (struct sockaddr *)&client_addr, &client_addr_len);

        if (new_client_socket < 0) {
            perror("Error in accepting other clients");
            return 1;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        printf("Accepted connection from client: %s\n", client_ip);

        struct ConnectionArgs conn_args;
        conn_args.new_client_socket = new_client_socket;
        conn_args.client_ip = client_ip;
        pthread_t thread;

        if (pthread_create(&thread, NULL, connection_handler, &conn_args) != 0) {
            perror("Thread creation failed");
            // Handle thread creation error
        }
        pthread_detach(thread);
    }
    for (int i = 0; i < num_neighbors; i++) {
        pthread_join(connect_thread[i], NULL);
    }

    // Free allocated memory for neighbors
    for (int i = 0; i < num_neighbors; i++) {
        free(neighbors[i]);
    }

    return 0;
}
