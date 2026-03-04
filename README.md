# CServer

Multi-threaded HTTP server using custom thread pool and BSD sockets.

## Building from Source

**Prerequisites**:

- Unix-like OS (Linux, FreeBSD)
- C Compiler (GCC or Clang)
- [Meson](https://mesonbuild.com/SimpleStart.html)
- Ninja (for Meson)
- git (optional, to clone repository)

Installing dependencies for Ubuntu:

```sh
sudo apt install build-essential meson ninja-build git
```

**Compilation** (run in project directory):

```sh
meson setup builddir
meson compile -C builddir
```

## Usage

```sh
./builddir/cserver
```

### Example output:

```text
[20:59:58.297] [INFO] setup_server:47: binding to ::
[20:59:58.297] [INFO] main:156: waiting for connections...
[21:00:09.466] [INFO] server_loop:111: got connection from ::1
[21:00:09.468] [INFO] networktask_client_handler:135: closing connection...
```

*Use Ctrl+C to stop.*

## Testing

### Using Web Browser

Depending on the *bind* address, use `::1` (IPv6) or `127.0.0.1` (IPv4) as IP.

Run `cserver` then open one of those links in your web browser: `http://[::1]:3490` or `http://127.0.0.1:3490`

### Using curl

```sh
curl '[::1]:3490'
curl '127.0.0.1:3490'
# raw HTML output
```

## Features

**General:**

- Thread Pool pattern for handling concurrent connections
- Basic HTTP/1.1 GET request handling

**Security:**

- Path traversal protection (`..` checks)
- Header size limits (returns 431 if too large)
- Socket timeouts to prevent hanging connections
     
## Limitations

- No TLS/SSL: do not use for sensitive data
- No URL-encoding: use latin, underscore `_`, hyphen `-` and period `.` symbols for file names
- No kqueue/epoll: not suitable for high load scenarios
- No DDoS protection
