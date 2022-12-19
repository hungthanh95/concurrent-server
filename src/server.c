#include "server.h"

// each peer is identified by the file descriptor
peer_state_t global_state[MAXFDS];

// These constants make creating fd_status_t values less verbose.
const fd_status_t fd_status_R = {.want_read = true, .want_write = false};
const fd_status_t fd_status_W = {.want_read = false, .want_write = true};
const fd_status_t fd_status_RW = {.want_read = true, .want_write = true};
const fd_status_t fd_status_NORW = {.want_read = false, .want_write = false};



void serve_connection(int sockfd) {
    /* Client attempting to connect and send data will succeed even before the
     * the connection is accept()-ed by the server. Therefore, to better simulate
     * blocking of other clients while one is being served, do this 'ack' from
     * the server which the client expects to see before proceeding. */
    if (send(sockfd, "*", 1, 0) < 1) {
        perror_die("send");
    }

    ProcessingState state = WAIT_FOR_MSG;

    while (1) {
        uint8_t buf[1024];
        int len = recv(sockfd, buf, sizeof(buf), 0);
        if (len < 0) {
            perror_die("recv");
        } else if (len == 0) {
            break;
        }

        for (int i = 0; i < len; i++) {
            switch (state) {
            case WAIT_FOR_MSG:
                if (buf[i] == '^') {
                    state = IN_MSG;
                }
                break;
            case IN_MSG:
                if (buf[i] == '$') {
                    state = WAIT_FOR_MSG;
                } else {
                    buf[i] += 1;
                    if (send(sockfd, &buf[i], 1, 0) < 1) {
                        perror("send error");
                        close(sockfd);
                        return;
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    close(sockfd);
}



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


fd_status_t on_peer_ready_recv(int sockfd) 
{
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