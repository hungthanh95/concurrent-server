#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/epoll.h>

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

    int epollfd = epoll_create1(0);
    if (epollfd < 0) {
        perror_die("epoll_create1");
    }

    struct epoll_event accept_event;
    accept_event.data.fd = listener_sockfd;
    accept_event.events = EPOLLIN;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listener_sockfd, &accept_event) < 0) {
        perror_die("epoll_ctl EPOLL_CTL_ADD");
    }

    struct epoll_event* events = calloc(MAXFDS, sizeof(struct epoll_event));
    if (events == NULL) {
        die("Unable to allocate memeory for epoll_events");
    }

    while (1) {

        int nready = epoll_wait(epollfd, events, MAXFDS, -1);

        // nready return number of fd is ready
        for (int i = 0; i < nready; i++) {
            // check if epoll error
            if (events[i].events & EPOLLERR) {
                perror_die("epoll_wait returned EPOLLERR");
            }

            // check if this fd became readable
            if (events[i].data.fd == listener_sockfd) {

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
                    if (newsockfd > MAXFDS) {
                        die("socket fd (%d) >= MAXFDS (%d)", newsockfd, MAXFDS);
                    }

                    fd_status_t status = on_peer_connected(newsockfd, &peer_addr, peer_addr_len);
                    struct epoll_event event = {0};
                    event.data.fd = newsockfd;
                    if (status.want_read) {
                        event.events |= EPOLLIN;
                    }
                    if (status.want_write) {
                        event.events |= EPOLLOUT;
                    }

                    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, newsockfd, &event) < 0) {
                        perror_die("epoll_ctl EPOLL_CTL_ADD");
                    }
                }
            } else {
                //  A peer socket is ready
                if (events[i].events & EPOLLIN) {
                    // ready to reading
                    int fd = events[i].data.fd;
                    fd_status_t status = on_peer_ready_recv(fd);
                    struct epoll_event event = {0};
                    event.data.fd = fd;
                    
                    if (status.want_read) {
                    event.events |= EPOLLIN;
                    }
                    if (status.want_write) {
                        event.events |= EPOLLOUT;
                    }
                    if (event.events == 0) {
                        printf("socket %d closing\n", fd);
                        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
                            perror_die("epoll_ctl EPOLL_CTL_DEL");
                        }
                        close(fd);
                    } else if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0) {
                        perror_die("epoll_ctl EPOLL_CTL_MOD");
                    }
                } else if (events[i].events & EPOLLOUT) {
                    // ready for writing
                    int fd = events[i].data.fd;
                    fd_status_t status = on_peer_ready_send(fd);
                    struct epoll_event event = {0};
                    event.data.fd = fd;

                    if (status.want_read) {
                        event.events |= EPOLLIN;
                    }
                    if (status.want_write) {
                        event.events |= EPOLLOUT;
                    }
                    if (event.events == 0) {
                        printf("socket %d closing\n", fd);
                        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
                            perror_die("epoll_ctl EPOLL_CTL_DEL");
                        }
                        close(fd);
                    } else if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0) {
                        perror_die("epoll_ctl EPOLL_CTL_MOD");
                    }
                }
            }
        }  
    }
    return 0;
}
