#include "connection.h"
#include "customlog.h"
#include "threadpool.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT "3490"

static int setup_server(void) {
  struct addrinfo hints;
  struct addrinfo *servinfo;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; // Either IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  int rv;
  if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
    LOG_FATAL("gai err: %s", gai_strerror(rv));
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
    LOG_INFO("binding to %s", ipstr);

    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      LOG_ERRNO("socket");
      continue;
    }

    const int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
      LOG_FATAL_ERRNO("setsockopt");
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      LOG_ERRNO("bind");
      continue;
    }

    break;
  }
  freeaddrinfo(servinfo);

  if (p == NULL) {
    LOG_FATAL("failed to bind");
  }

  if (listen(sockfd, BACKLOG) == -1) {
    LOG_FATAL("listen");
  }
  return sockfd;
}

static int server_loop(int server_fd, ThreadPool **thread_pool) {
  while (1) {
    struct sockaddr_storage their_addr;
    socklen_t sin_size = sizeof their_addr;
    int new_fd = accept(server_fd, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) {
      LOG_ERRNO("accept");
      continue;
    }

    char s[INET6_ADDRSTRLEN];
    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr),
              s, sizeof s);
    LOG_INFO("got connection from %s", s);

    NetworkTask *task = malloc(sizeof(*task));
    if (task == NULL) {
      close(new_fd);
      LOG_ERRNO("malloc");
      continue;
    }

    task->client_fd = new_fd;
    if (threadpool_push(*thread_pool, networktask_send_html, task) != 0) {
      LOG_ERROR("threadpool_push");
    }
  }
}

int main(void) {
  int server_fd = setup_server(); // errors handling inside

  ThreadPool *thread_pool = threadpool_init(NUM_THREADS);
  if (thread_pool == NULL) {
    LOG_FATAL("thread_pool is NULL");
  }
  LOG_INFO("waiting for connections...");

  server_loop(server_fd, &thread_pool);

  // TODO: greaceful shutdown
  // threadpool_destroy(thread_pool);
}
