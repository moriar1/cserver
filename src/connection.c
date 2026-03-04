#include "connection.h"
#include "customlog.h"
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/sendfile.h>
#endif

void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int send_all(int fd, const char *buf, size_t len) {
  size_t sent = 0;
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
  return 0;
}

static void send_400(int fd) {
  static const char m[] =
      "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
  if (send_all(fd, m, sizeof(m) - 1) == -1) {
    LOG_ERRNO("send 400");
  }
}

static void send_403(int fd) {
  static const char m[] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
  if (send_all(fd, m, sizeof(m) - 1) == -1) {
    LOG_ERRNO("send 403");
  }
}

static void send_404(int fd) {
  static const char m[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
  if (send_all(fd, m, sizeof(m) - 1) == -1) {
    LOG_ERRNO("send 404");
  }
}

static void send_405(int fd) {
  static const char m[] =
      "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
  if (send_all(fd, m, sizeof(m) - 1) == -1) {
    LOG_ERRNO("send 405");
  }
}

static void send_431(int fd) {
  static const char m[] = "HTTP/1.1 431 Request Header Fields Too "
                          "Large\r\nContent-Length: 0\r\n\r\n";
  if (send_all(fd, m, sizeof(m) - 1) == -1) {
    LOG_ERRNO("send 431");
  }
}

static void send_500(int fd) {
  static const char m[] =
      "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
  if (send_all(fd, m, sizeof(m) - 1) == -1) {
    LOG_ERRNO("send 500");
  }
}

void networktask_client_handler(void *arg) {
  NetworkTask *args = arg;
  int fd = args->client_fd;

  const struct timeval time = {.tv_sec = TIMEOUT, .tv_usec = 0};
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof(time))) {
    LOG_ERRNO("failed set rcv timout");
  }
  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &time, sizeof(time))) {
    LOG_ERRNO("failed set snd timout");
  }

  char recv_buf[MAXDATASIZE];
  size_t total_nbytes = 0;
  ssize_t numbytes = 0;
  while (1) {
    size_t spaceleft = sizeof(recv_buf) - total_nbytes - 1;
    numbytes = recv(fd, recv_buf + total_nbytes, spaceleft, 0);

    // Err or disconnect
    if (numbytes < 0) {
      if (errno == EINTR) {
        LOG_ERRNO("recv EINTR");
        continue;
      }
      if (errno == EAGAIN) {
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

    // Too long headers => 431
    if (total_nbytes >= sizeof(recv_buf) - 2) {
      send_431(fd);
      goto cleanup;
    }
  }
  handle_http_request(fd, recv_buf);

cleanup:
  LOG_INFO("closing connection...");
  close(fd);
  free(arg);
}

__attribute__((pure)) static const char *get_mime_type(const char *restrict pth,
                                                       size_t len) {
  if (len < 4) {
    return "application/octet-stream";
  }
  if (strncmp(pth + len - 4, ".html", 5) == 0 ||
      strncmp(pth + len - 3, ".htm", 4) == 0) {
    return "text/html";
  }
  if (strncmp(pth + len - 4, ".jpeg", 5) == 0 ||
      strncmp(pth + len - 3, ".jpg", 4) == 0) {
    return "image/jpeg";
  }
  if (strncmp(pth + len - 3, ".png", 4) == 0) {
    return "image/png";
  }
  if (strncmp(pth + len - 3, ".css", 3) == 0) {
    return "text/css";
  }
  if (strncmp(pth + len - 2, ".js", 2) == 0) {
    return "application/javascript";
  }
  return "application/octet-stream";
}

int handle_http_request(int fd, const char *recv_buf) {
  // Not GET => 405
  if (strncmp(recv_buf, "GET ", 4) != 0) {
    send_405(fd);
    return -1;
  }
  recv_buf += 4; // Skip `GET `

  // --- Extract path ---
  const char *ptr;
  if ((ptr = strchr(recv_buf, ' ')) == NULL) {
    send_400(fd); // not found path => 400
    return -1;
  }

  size_t path_len = ptr - recv_buf; // `\0` and `/` counts too
  if (path_len == 0 || path_len > PATH_MAXLEN) {
    send_400(fd); // path issue => 400
    return -1;
  }
  // +2 for dot at the begining ("./index.html") and \0 at the end
  char path[PATH_MAXLEN + 2];
  path[0] = '.';
  memcpy(path + 1, recv_buf, path_len);
  path[path_len + 1] = 0;

  // request with `../../` in path => 403
  if (strstr(path, "..") != NULL) {
    send_403(fd);
    return -1;
  }
  // response index.html for `GET /`
  // path[0] is always '.'
  if (path_len == 1 && path[1] == '/') {
    const char *index_file = "./index.html";
    strcpy(path, index_file);
    path_len = 11;
  }

  struct stat st;
  if (stat(path, &st) == -1) {
    if (errno == ENOENT) {
      send_404(fd);
      return -1;
    }
    LOG_ERRNO("stat");
    send_500(fd);
    return -1;
  }
  ssize_t content_length = st.st_size;
  const char *mime = get_mime_type(path, path_len);

  // OK => 200
  char headers[128];
  long sz = snprintf(
      headers, sizeof(headers),
      "HTTP/1.1 200 OK\r\nContent-Length: %zd\r\nContent-Type: %s\r\n\r\n",
      content_length, mime);
  if (sz < 0 || sz >= (long)sizeof(headers)) {
    LOG_ERROR("snprintf header");
    send_500(fd);
    return -1;
  }

  // Sending headers
  if (send_all(fd, headers, sz) == -1) {
    LOG_ERRNO("send header");
    return -1;
  }

  // Sending requested file

  int content_fd = open(path, O_RDONLY);
  if (content_fd == -1) {
    LOG_ERRNO("open");
    return -1;
  }

#ifdef __linux__
  ssize_t total_sent = 0;
  ssize_t sent = 1;

  while (sent > 0) {
    if ((sent = sendfile(fd, content_fd, NULL, content_length - total_sent)) ==
        -1) {
      if (errno == EAGAIN) {
        LOG_INFO("client timeout (couldn't sendfile)");
      } else {
        LOG_ERRNO("sendfile");
      }
      close(content_fd);
      return -1;
    }
    total_sent += sent;
  }
#elif defined __FreeBSD__
  // NOTE: in FreeBSD loop is required only for non-block I/O
  if (sendfile(content_fd, fd, 0, content_length, NULL, NULL, 0) == -1) {
    if (errno == EAGAIN) {
      LOG_INFO("client timeout (couldn't sendfile)");
    } else {
      LOG_ERRNO("sendfile");
    }
    close(content_fd);
    return -1;
  }
#endif
  close(content_fd);
  return 0;
}
