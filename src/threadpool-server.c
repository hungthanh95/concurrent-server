#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include "thread-pool.h"
#include "server.h"


typedef struct { int sockfd; } thread_config_t;


void server_thread(void* arg) {
  thread_config_t* config = (thread_config_t*)arg;
  int sockfd = config->sockfd;
  free(config);

  // This cast will work for Linux, but in general casting pthread_id to an
  // integral type isn't portable.
  unsigned long id = (unsigned long)pthread_self();
  printf("Thread %lu created to handle connection with socket %d\n", id,
         sockfd);
  serve_connection(sockfd);
  printf("Thread %lu done\n", id);
}

int main(int argc, char const *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int portnum = 9090;
    if (argc >= 2) {
        portnum = atoi(argv[1]);
    }

    int num_threads = 5;
    if (argc >=3) {
        num_threads = atoi(argv[2]);
    }

    printf("Serving on port %d\n", portnum);
    fflush(stdout);

    printf("Making threadpool with %d threads\n", num_threads);
    threadpool_* threadpool = threadpool_init(num_threads);

    int sockfd = listen_inet_socket(portnum);

    while (1) {
        struct sockaddr_in peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);

        int newsockfd =
            accept(sockfd, (struct sockaddr*)&peer_addr, &peer_addr_len);

        if (newsockfd < 0) {
            perror_die("ERROR on accept");
        }

        report_peer_connected(&peer_addr, peer_addr_len);

        thread_config_t* config = (thread_config_t*)malloc(sizeof(*config));
        if (!config) {
            die("OOM");
        }
        config->sockfd = newsockfd;
        threadpool_add_work(threadpool, server_thread, (void*)(thread_config_t*)config);
    }


    threadpool_wait(threadpool);
    printf("Killing threadpool\n");
    threadpool_destroy(threadpool);

    return 0;
}
