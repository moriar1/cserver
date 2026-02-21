#include "connection.h"
#include "customlog.h"
#include <netinet/in.h>
#include <stddef.h>
#include <sys/time.h>
#include <unistd.h>

void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int send_all(int fd, const char *buf, long len) {
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

void networktask_send_html(void *arg) {
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

    // Too long headers
    if (total_nbytes >= sizeof(recv_buf) - 2) {
      LOG_DEBUG("too long headers");
      break;
      // send_404(fd); // TODO: send another 4xx
      // goto cleanup;
    }
  }
  handle_http_request(fd, recv_buf);

cleanup:
  LOG_INFO("closing connection...");
  close(fd);
  free(arg);
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

void send_404(int fd) {
  const char *msg = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n";
  if (send_all(fd, msg, (long)strlen(msg)) == -1) {
    LOG_ERRNO("send 404");
  }
}

int handle_http_request(int fd, const char *recv_buf) {
  if (strncmp(recv_buf, "GET ", 4) != 0) {
    send_404(fd);
    return -1;
  }
  recv_buf += 4; // Skip `GET `

  // Extract path
  const char *ptr;
  if ((ptr = strstr(recv_buf, " ")) == NULL) {
    send_404(fd); // TODO: 4xx
    return -1;
  }

  ptrdiff_t path_len = ptr - recv_buf;
  if (path_len == 0 || path_len > 128) {
    send_404(fd);
  }
  // +2 for dot at the begining ("./index.html") and \0 at the end
  char *path = malloc(sizeof(char) * (path_len + 2));
  if (path == NULL) {
    LOG_ERRNO("path malloc");
    return -1;
  }
  path[0] = '.';
  memcpy(path + 1, recv_buf, path_len);
  path[path_len + 1] = 0;

  // skip requests with `../../` in path
  if (strstr(path, "..") != NULL) {
    send_404(fd); // TODO: 4xx
    free(path);
    return -1;
  }
  if (path_len == 1 && path[1] == '/') {
    free(path); // no need in realloc (because of changing buffer)
    path = malloc(11);
    if (path == NULL) {
      LOG_ERRNO("realloc");
      return -1;
    }
    snprintf(path, 11, "index.html");
  }

  // Read requested file
  char *content = NULL;
  long content_lenght = read_file(path, &content);
  free(path);

  if (content_lenght < 0) {
    // Failed reading file => 404
    LOG_ERROR("failed to read content");
    send_404(fd);
    return -1;
  }
  // OK => 200
  char send_headers[128];
  long sz = snprintf(send_headers, sizeof(send_headers),
                     "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n",
                     content_lenght);
  if (sz < 0 || sz >= (long)sizeof(send_headers)) {
    LOG_ERROR("snprintf header");
    send_404(fd); // better send 5xx err code
    free(content);
    return -1;
  }

  if (send_all(fd, send_headers, sz) == -1) {
    LOG_ERRNO("send header");
    free(content);
    return -1;
  }
  if (send_all(fd, content, content_lenght) == -1) {
    LOG_ERRNO("send body");
    free(content);
    return -1;
  }

  free(content);
  return 0;
}
