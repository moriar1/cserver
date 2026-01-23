# CServer

Multi-threaded HTTP server using custom thread pool and BSD sockets.

## Building from Source

**Prerequisites**:

- Unix-like OS (Linux, *BSD)
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
server: binding to ::
server: waiting for connections...
server: got connection from ::1
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
# raw html output
```
