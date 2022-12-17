#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "utils.h"


typedef enum { WAIT_FOR_MSG, IN_MSG } ProcessingState;

void serve_connection(int sockfd);

#endif /* SERVER_H */