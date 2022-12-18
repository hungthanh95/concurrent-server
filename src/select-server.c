#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

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


// each peer is identified by the file descriptor
peer_state_t global_state[MAXFDS];

// These constants make creating fd_status_t values less verbose.
const fd_status_t fd_status_R = {.want_read = true, .want_write = false};
const fd_status_t fd_status_W = {.want_read = false, .want_write = true};
const fd_status_t fd_status_RW = {.want_read = true, .want_write = true};
const fd_status_t fd_status_NORW = {.want_read = false, .want_write = false};


fd_status_t on_peer_connected(int sockfd, const struct sockaddr_in* peer_addr, socklen_t peer_addr_len)
{
    assert(sockfd < MAXFDS);
    report_peer_connected(peer_addr, peer_addr_len);

    // Initialize state to send back a '*' to the peer imediately
    peer_state_t* peer_state = &global_state[sockfd];
    peer_state->state = INITIAL_ACK;
    peer_state->sendbuf[0] = '*';
    peer_state->sendptr = 0;
    peer_state->sendbuf_end = 1;

    // signal that this socket is ready for writing
    return fd_status_W;
}


fd_status_t on_peer_ready_recv(int sockfd) {
    assert(sockfd < MAXFDS);
    peer_state_t* peer_state = &global_state[sockfd];

    if (peer_state->state == INITIAL_ACK || peer_state->sendptr < peer_state->sendbuf_end) {
        // Until the initial ACK has been sent to the peer or
        //  until all data staged for sending
        return fd_status_W;
    }
    uint8_t buf[1024];
    int nbytes = recv(sockfd, buf, sizeof(buf), 0);
    if (nbytes == 0) {
        // the peer disconnected
        return fd_status_NORW;
    } else if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // the socket is not ready for recv. wait until it is
            return fd_status_R;
        }  else {
            perror_die("recv");
        }
    }
    bool ready_to_send = false;
    for (int i = 0; i < nbytes; ++i) {
        switch (peer_state->state) {
        case INITIAL_ACK:
            assert(0 && "can't reach here");
            break;
        
        case WAIT_FOR_MSG:
            if (buf[i] == '^') {
                peer_state->state = IN_MSG;
            }
            break;
        
        case IN_MSG:
            if (buf[i] == '$') {
                peer_state->state = WAIT_FOR_MSG;
            } else {
                assert(peer_state->sendbuf_end < SENDBUF_SIZE);
                peer_state->sendbuf[peer_state->sendbuf_end++] = buf[i] + 1;
                ready_to_send = true;
            }
            break;

        default:
            break;
        }
    }
    
    return (fd_status_t) {.want_read = !ready_to_send,
                          .want_write = ready_to_send};
}

fd_status_t on_peer_ready_send(int sockfd)
{
    assert(sockfd < MAXFDS);
    peer_state_t* peer_state = &global_state[sockfd];

    if (peer_state->sendptr >= peer_state->sendbuf_end) {
        // nothing to send
        return fd_status_RW;
    }
    int send_len = peer_state->sendbuf_end - peer_state->sendptr;
    int nsent = send(sockfd, &peer_state->sendbuf[peer_state->sendptr], send_len, 0);
    if (nsent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return fd_status_W;
        } else {
            perror_die("send");
        }
    }

    if (nsent < send_len) {
        peer_state->sendptr += nsent;
        return fd_status_W;
    } else {
        // everything was sent successfully, reset the send queue
        peer_state->sendptr = 0;
        peer_state->sendbuf_end = 0;

        // special case state transition in if we ware in INITAL_ACK until now
        if (peer_state->state == INITIAL_ACK) {
            peer_state->state = WAIT_FOR_MSG;
        }

        return fd_status_R;
    }
}

int main(int argc, char const *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int port_num = 9090;
    if (argc >= 2) {
        port_num = atoi(argv[1]);
    }
    printf("Serving on port %d\n", port_num);

    int listener_sockfd = listen_inet_socket(port_num);

    make_socket_non_blocking(listener_sockfd);

    if (listener_sockfd >= FD_SETSIZE) {
        die("listener socket fd (%d) >= FD_SETSIZE (%d)", listener_sockfd, FD_SETSIZE);
    }

    // Tracking which FDs we want to monitor for reading and writing
    fd_set readfds_master;
    FD_ZERO(&readfds_master);

    fd_set writefds_master;
    FD_ZERO(&writefds_master);

    // listening socket is always monitored for read to detect when new peer connection are incoming
    FD_SET(listener_sockfd, &readfds_master);

    // For more efficiency, fdset_max tracks the maximal FD seem so far,
    // this make it unnecessary for select to iterate all FD_SETSIZE on every call
    int fdset_max = listener_sockfd;

    while (1) {
        // select() modifies the fd_sets passed to it, so we have to pass in copies
        fd_set readfds = readfds_master;
        fd_set writefds = writefds_master;

        int nready = select(fdset_max + 1, &readfds, &writefds, NULL, NULL);
        if (nready < 0) {
            perror_die("select");
        }

        // nready tells us the total number of ready events; if one socket is both
        // readable and writeable it will be 2.
        for (int fd = 0; fd <= fdset_max && nready > 0; fd++) {
            // check if this fd became readable
            if (FD_ISSET(fd, &readfds)) {
                nready--;

                if (fd == listener_sockfd) {
                    // the listening socket is ready; this means a new peer is connecting
                    struct sockaddr_in peer_addr;
                    socklen_t peer_addr_len = sizeof(peer_addr);
                    int newsockfd = accept(listener_sockfd, (struct sockaddr*)&peer_addr, &peer_addr_len);
                    
                    if (newsockfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // this can happen due to nonblocking socket mode
                            printf("accept returned EAGAIN or EWOULDBLOCK\n");
                        } else {
                            perror_die("accept");
                        }
                    } else {
                        make_socket_non_blocking(newsockfd);

                        if (newsockfd > fdset_max) {
                            if (newsockfd >= FD_SETSIZE) {
                                die("socket fd (%d) >= FD_SETSIZE (%d)", newsockfd, FD_SETSIZE);
                            }
                            fdset_max = newsockfd;
                        }

                        fd_status_t status = on_peer_connected(newsockfd, &peer_addr, peer_addr_len);
                        if (status.want_read) {
                            FD_SET(newsockfd, &readfds_master);
                        } else {
                            FD_CLR(newsockfd, &readfds_master);
                        }

                        if (status.want_write) {
                            FD_SET(newsockfd, &writefds_master);
                        } else {
                            FD_CLR(newsockfd, &writefds_master);
                        }
                    }
                } else {
                    fd_status_t status = on_peer_ready_recv(fd);
                    if (status.want_read) {
                        FD_SET(fd, &readfds_master);
                    } else {
                        FD_CLR(fd, &readfds_master);
                    }

                    if (status.want_write) {
                        FD_SET(fd, &writefds_master);
                    } else {
                        FD_CLR(fd, &writefds_master);
                    }

                    if (!status.want_read && !status.want_write) {
                        printf("socket %d closing\n", fd);
                        close(fd);
                    }
                }
            }
            // check if this fd became writeable
            if (FD_ISSET(fd, &writefds)) {
                nready--;
                fd_status_t status = on_peer_ready_send(fd);
                if (status.want_read) {
                    FD_SET(fd, &readfds_master);
                } else {
                    FD_CLR(fd, &readfds_master);
                }

                if (status.want_write) {
                    FD_SET(fd, &writefds_master);
                } else {
                    FD_CLR(fd, &writefds_master);
                }
                if (!status.want_read && !status.want_write) {
                    printf("socket %d closing\n", fd);
                    close(fd);
                }
            }
        }  
    }

    return 0;
}
