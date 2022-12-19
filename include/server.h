#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>

#include "utils.h"


// Max FDS on linux is 1024
#define MAXFDS              1000
#define SENDBUF_SIZE        1024

typedef enum {INITIAL_ACK, WAIT_FOR_MSG, IN_MSG } ProcessingState;

typedef struct {
    ProcessingState state;
    uint8_t sendbuf[SENDBUF_SIZE];          /* Contains data the server has to send back to client */
    int sendbuf_end;                        /* Point to last valid byte in buffer */
    int sendptr;                            /* Point to next byte to send */
} peer_state_t;

// Callback return this status to main loop
typedef struct {
    bool want_read;                         /* True: mean we want to keep monitoring this fd for reading */
    bool want_write;                        /* True: mean we want to keep monitoring this fd for writing */
} fd_status_t;



void serve_connection(int sockfd);
fd_status_t on_peer_ready_recv(int sockfd);
fd_status_t on_peer_ready_send(int sockfd);
fd_status_t on_peer_connected(int sockfd, const struct sockaddr_in* peer_addr, socklen_t peer_addr_len);

#endif /* SERVER_H */