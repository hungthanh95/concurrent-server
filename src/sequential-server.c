#include "server.h"

int main(int argc, char** argv) {
    /* Change the buffer mode of 'stdout' to no buffering (_IONBF) and resize internal buffer to 0 */
    setvbuf(stdout, NULL, _IONBF, 0);
    
    int port_num = 9090;
    if (argc >= 2) {
        port_num = atoi(argv[1]);
    }

    printf("Serving on port %d\n", port_num);

    int sockfd = listen_inet_socket(port_num);

    while (1) {
        struct sockaddr_in peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);

        // create a new connected socket which socket is connect to server
        int newsocketfd = accept(sockfd, (struct sockaddr*)&peer_addr, &peer_addr_len);

        if (newsocketfd < 0) {
            perror_die("ERROR on accept");
        }

        report_peer_connected(&peer_addr, peer_addr_len);
        serve_connection(newsocketfd);
        printf("peer done\n");
    }

    return 0;
}

