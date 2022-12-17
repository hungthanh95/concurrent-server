#include "utils.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#define _GNU_SOURCE
#include <netdb.h>

#define N_BACKLOG 64


void die(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}


void* xmalloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        die("malloc failed");
    }
    return ptr;
}

void perror_die(char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void report_peer_connected(const struct sockaddr_in* sa, socklen_t salen) {
    char hostbuf[NI_MAXHOST];
    char portbuf[NI_MAXHOST];
    if (getnameinfo((struct sockaddr*)sa, salen, hostbuf, NI_MAXHOST, portbuf, NI_MAXSERV, 0) == 0) {
        printf("peer (%s, %s) connected\n", hostbuf, portbuf);
    } else {
        printf("peer (unknown) connected\n");
    }
}


int listen_inet_socket(int portnum) {
  // create socket with AF_INET; IPv4 internet protocol with socket stream
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror_die("ERROR opening socket");
  }

  // This helps avoid spurious EADDRINUSE when the previous instance of this
  // server died.
  int opt = 1;
  // provides an application program with the means to control socket behavior
  // like: allocate buffer space, control timeouts, or permit socket,...
  // THis function shall set the option specified SO_REUSEADDR at SOL_SOCKET protocol
  // level
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror_die("setsockopt");
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portnum);

  // bind() assigns the address specified by serv_addr to the socket
  if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror_die("ERROR on binding");
  }

  // mark the socket referred to by sockfd as passive socket
  // socket will be used to accept incoming connection request.
  if (listen(sockfd, N_BACKLOG) < 0) {
    perror_die("ERROR on listen");
  }

  return sockfd;
}

void make_socket_non_blocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1) {
    perror_die("fcntl F_GETFL");
  }

  if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror_die("fcntl F_SETFL O_NONBLOCK");
  }
}