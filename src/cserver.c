#include "threadpool.h"
#include <arpa/inet.h>
#include <err.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT "3490"
#define BACKLOG 10 // how many pending connections queue will hold
#define MAXDATASIZE 4096
enum { NUM_THREADS = 6 };

typedef struct {
  int client_fd;
} NetworkTask;

static void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

static void netoworktask_echo(void *arg) {
  NetworkTask *args = arg;
  long numbytes = 0;
  char buf[MAXDATASIZE];
  while (1) {
    numbytes = recv(args->client_fd, buf, sizeof buf, 0);
    if (numbytes > 0) {
      if (send(args->client_fd, buf, (size_t)numbytes, 0) == -1) {
        warn("send");
      }
    } else if (numbytes == 0) {
      // printf("server: %s disconnected\n", s);
      break;
    } else {
      warn("recv");
      break;
    }
  }
  printf("server: closing connection...\n");
  close(args->client_fd);
  free(arg);
}

int main(void) {
  struct addrinfo hints;
  struct addrinfo *servinfo;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; // Either IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  int rv;
  if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
    err(1, "gai err: %s\n", gai_strerror(rv));
  }

  // loop through all the results and bind to the first we can
  int sockfd;
  struct addrinfo *p;
  for (p = servinfo; p != NULL; p = p->ai_next) {
    void *addr;
    if (p->ai_family == AF_INET) { // IPv4
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
      addr = &(ipv4->sin_addr);
    } else { // IPv6
      struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
      addr = &(ipv6->sin6_addr);
    }
    char ipstr[INET6_ADDRSTRLEN];
    inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
    printf("server: binding to %s\n", ipstr);

    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      warn("socket");
      continue;
    }

    const int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
      err(2, "setsockopt");
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      warn("server:bind");
      continue;
    }

    break;
  }
  freeaddrinfo(servinfo);

  if (p == NULL) {
    err(3, "failed to bind");
  }

  if (listen(sockfd, BACKLOG) == -1) {
    err(4, "listen");
  }

  ThreadPool *thread_pool = threadpool_init(NUM_THREADS);
  if (thread_pool == NULL) {
    err(7, "thread_pool is NULL");
  }

  puts("server: waiting for connections...");

  while (1) {
    struct sockaddr_storage their_addr;
    socklen_t sin_size = sizeof their_addr;
    int new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) {
      warn("accept");
      continue;
    }

    char s[INET6_ADDRSTRLEN];
    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr),
              s, sizeof s);
    printf("server: got connection from %s\n", s);

    NetworkTask *task = malloc(sizeof(*task));
    if (task == NULL) {
      close(new_fd);
      warn("malloc");
      continue;
    }

    task->client_fd = new_fd;
    threadpool_push(thread_pool, netoworktask_echo, task);
  }
}
