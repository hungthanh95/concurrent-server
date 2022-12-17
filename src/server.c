#include "server.h"

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