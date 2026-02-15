#include "customlog.h"
#include "threadpool.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT "3490"
enum {
  NUM_THREADS = 6,
  MAXDATASIZE = 4096,
  BACKLOG = 10, // How many pending connections queue will hold
};

typedef struct {
  int client_fd;
} NetworkTask;

static void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// NOTE: if error occured ptr is unchanged
static long read_file(const char *file_path, char **ptr) {
  // Open file
  FILE *fp = fopen(file_path, "rb");
  if (fp == NULL) {
    LOG_ERRNO("fopen `%s`", file_path);
    return -1;
  }

  // Get filesize
  if (fseek(fp, 0, SEEK_END) != 0) {
    LOG_ERRNO("fseek end `%s`", file_path);
    fclose(fp);
    return -1;
  }
  long size = ftell(fp);
  if (size < 0) {
    LOG_ERRNO("ftell `%s`", file_path);
    fclose(fp);
    return -1;
  }
  if (size == 0) {
    *ptr = NULL;
    fclose(fp);
    return 0;
  }
  if (fseek(fp, 0L, SEEK_SET) != 0) {
    LOG_ERRNO("fseek set `%s`", file_path);
    fclose(fp);
    return -1;
  }

  // read file
  char *buf = malloc(size);
  if (buf == NULL) {
    LOG_ERRNO("malloc `%s`", file_path);
    fclose(fp);
    return -1;
  }
  if (fread(buf, size, 1, fp) != 1) {
    fclose(fp);
    free(buf);
    LOG_ERRNO("fread `%s`", file_path);
    return -1;
  }

  // Return buffer and its size
  *ptr = buf;
  fclose(fp);
  return size;
}

static void send_404(int fd) {
  const char *msg = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n";
  if (send(fd, msg, strlen(msg), 0) == -1) {
    LOG_ERRNO("send");
  }
}

static void networktask_send_html(void *arg) {
  NetworkTask *args = arg;
  int fd = args->client_fd;
  long numbytes = 0;
  char recv_buf[MAXDATASIZE];
  char *content = NULL;

  numbytes = recv(fd, recv_buf, MAXDATASIZE - 1, 0);
  if (numbytes <= 0) {
    if (numbytes < 0) {
      LOG_ERRNO("recv");
    }
    goto cleanup;
  }

  recv_buf[numbytes] = 0; // for strncmp
  if (strncmp(recv_buf, "GET", 3) != 0) {
    // If not GET request => 404
    send_404(fd);
    goto cleanup;
  }

  long content_lenght = read_file("index.html", &content);

  if (content_lenght < 0) {
    // Failed reading file => 404
    LOG_ERROR("failed to read content");
    send_404(fd);
    goto cleanup;
  }
  // OK => 200
  char send_headers[128];
  long sz = snprintf(send_headers, sizeof(send_headers),
                     "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n",
                     content_lenght);
  if (sz < 0 || sz >= (long)sizeof(send_headers)) {
    LOG_ERROR("snprintf header");
    send_404(fd); // better send 5xx err code
    goto cleanup;
  }

  if (send(fd, send_headers, sz, 0) == -1) {
    LOG_ERRNO("send header");
    goto cleanup;
  }
  if (send(fd, content, content_lenght, 0) == -1) {
    LOG_ERRNO("send body");
    goto cleanup;
  }

cleanup:
  if (content) {
    free(content);
  }
  LOG_INFO("closing connection...");
  close(fd);
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
      LOG_ERRNO("server:bind");
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

  ThreadPool *thread_pool = threadpool_init(NUM_THREADS);
  if (thread_pool == NULL) {
    LOG_FATAL("thread_pool is NULL");
  }

  LOG_INFO("waiting for connections...");

  while (1) {
    struct sockaddr_storage their_addr;
    socklen_t sin_size = sizeof their_addr;
    int new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
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
    threadpool_push(thread_pool, networktask_send_html, task);
  }
}
