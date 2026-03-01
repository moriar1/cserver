#ifndef NETWORK_H
#define NETWORK_H

#include <sys/socket.h>

enum {
  NUM_THREADS = 6,
  MAXDATASIZE = 4096,
  TIMEOUT = 10,
  BACKLOG = 10, // How many pending connections queue will hold
};

typedef struct {
  int client_fd;
} NetworkTask;

void *get_in_addr(struct sockaddr *sa);
int send_all(int fd, const char *buf, size_t len);
void networktask_client_handler(void *arg);
int handle_http_request(int fd, const char *recv_buf);

#endif
