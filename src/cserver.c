#include "customlog.h"
#include "threadpool.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PORT "3490"
enum {
  NUM_THREADS = 6,
  MAXDATASIZE = 4096,
  TIMEOUT = 10,
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

static int send_all(int fd, const char *buf, long len) {
  ssize_t sent = 0;
  ssize_t n;

  while (sent < len) {
    if ((n = send(fd, buf + sent, len - sent, 0)) == -1) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    sent += n;
  }
  LOG_DEBUG("sent: %zd", sent);
  return 0;
}

static void send_404(int fd) {
  const char *msg = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n";
  if (send_all(fd, msg, (long)strlen(msg)) == -1) {
    LOG_ERRNO("send 404");
  }
}

static void networktask_send_html(void *arg) {
  NetworkTask *args = arg;
  int fd = args->client_fd;

  const struct timeval time = {.tv_sec = TIMEOUT, .tv_usec = 0};
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof(time))) {
    LOG_ERRNO("failed set rcv timout");
  }
  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &time, sizeof(time))) {
    LOG_ERRNO("failed set snd timout");
  }

  char *content = NULL;
  char recv_buf[MAXDATASIZE];
  size_t total_nbytes = 0;
  ssize_t numbytes = 0;
  while (true) {
    size_t spaceleft = sizeof(recv_buf) - total_nbytes - 1;
    numbytes = recv(fd, recv_buf + total_nbytes, spaceleft, 0);

    // Err or disconnect
    if (numbytes < 0) {
      if (errno == EINTR) {
        LOG_ERRNO("recv EINTR");
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        LOG_INFO("client timeout (no recv in %d seconds)", TIMEOUT);
      } else {
        LOG_ERRNO("recv");
      }
      goto cleanup;
    }
    if (numbytes == 0) {
      LOG_INFO("client disconnected");
      goto cleanup;
    }

    total_nbytes += numbytes;
    recv_buf[total_nbytes] = 0; // for strstr
    if (strstr(recv_buf, "\r\n\r\n") != NULL) {
      break; // found end of headers
    }

    // Too long headers
    if (total_nbytes >= sizeof(recv_buf) - 2) {
      LOG_DEBUG("too long headers");
      break;
      // send_404(fd); // TODO: send another 4xx
      // goto cleanup;
    }
  }

  if (strncmp(recv_buf, "GET", 3) != 0) {
    send_404(fd); // If not GET request => TODO: 4xx
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

  if (send_all(fd, send_headers, sz) == -1) {
    LOG_ERRNO("send header");
    goto cleanup;
  }
  if (send_all(fd, content, content_lenght) == -1) {
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

int main(void) {
  int server_fd = setup_server(); // errors handling inside

  ThreadPool *thread_pool = threadpool_init(NUM_THREADS);
  if (thread_pool == NULL) {
    LOG_FATAL("thread_pool is NULL");
  }

  LOG_INFO("waiting for connections...");

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
    threadpool_push(thread_pool, networktask_send_html, task);
  }
}
