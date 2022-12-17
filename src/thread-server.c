#include <pthread.h>
#include "server.h"

typedef struct { int sockfd; } thread_config_t;


void* server_thread(void* arg) {
    thread_config_t* config = (thread_config_t*)arg;
    int sockfd = config->sockfd;
    // This cast will work for linux
    unsigned long id = (unsigned long)pthread_self();
    printf("Thread %lu created to handle connection with socket %d\n", id, sockfd);
    serve_connection(sockfd);
    printf("Thread %lu done\n", id);
    return 0;
}

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
        
        pthread_t p_thread;
        thread_config_t* config = (thread_config_t*)malloc(sizeof(*config));
        if (!config) {
            die("OOM");
        }
        config->sockfd = newsocketfd;
        pthread_create(&p_thread, NULL, server_thread, config);

        // detach the thread - when it done, its resource will be cleaned up
        // since the main thread lives forever, it will outlive the serving threads
        pthread_detach(p_thread);
    }

    return 0;
}