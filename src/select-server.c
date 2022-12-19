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
#include "server.h"


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
