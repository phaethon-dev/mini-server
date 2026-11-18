# mini-server

[![CI](https://github.com/phaethon-dev/mini-server/actions/workflows/ci.yml/badge.svg)](https://github.com/phaethon-dev/mini-server/actions/workflows/ci.yml)

A small HTTP/1.1 static file server written in C++17 from raw sockets up, with no networking or HTTP libraries. It runs on both Windows and Linux. It parses requests strictly against the HTTP specification, serves files from a folder with the correct content types, and blocks attempts to read files outside that folder.

## Features

- **Strict request parsing** that follows RFC 9112: exactly three request-line parts separated by single spaces, a version matching `HTTP/<digit>.<digit>`, `\r\n` line endings, and a required blank line after the headers.
- **Header validation**: names may contain only letters, digits, and `-`; only spaces and tabs are trimmed from values; control characters and duplicate headers are rejected.
- **Static file serving** from a `public` folder, sent byte for byte so images and other binary files arrive intact.
- **Folder support**: requesting a folder serves its `index.html`.
- **Content types** chosen from the file extension, case-insensitively: HTML, CSS, JavaScript, JSON, PNG, JPEG, GIF, plain text, and PDF, with `application/octet-stream` as the fallback.
- **Percent-decoding** of request paths, so `/my%20photo.jpg` serves `my photo.jpg`. Malformed escapes such as `%zz`, an encoded NUL byte (`%00`), and an encoded slash (`%2F`) are rejected with `400 Bad Request`.
- **Path traversal protection** in two independent layers (see below).
- **`GET` and `HEAD`**: a `HEAD` request gets exactly the headers `GET` would, including the real `Content-Length`, but no body, so a client can check whether a file exists and how large it is without downloading it.
- **Browser caching**: every file is sent with a `Last-Modified` date. When a browser asks again with `If-Modified-Since` and the file hasn't changed, the server answers `304 Not Modified` with no body, and the browser reuses its cached copy.
- **Clear error responses**: `400 Bad Request`, `404 Not Found`, `405 Method Not Allowed` (with the required `Allow` header), and `500 Internal Server Error`, each with a small HTML page.
- **Robust networking**: requests that arrive in several pieces are reassembled, responses are sent completely even when the socket accepts only part at a time, and request headers are limited to 8 KB.
- **Timeouts**: a client that goes silent for 5 seconds, or takes longer than 10 seconds to send its whole request, is disconnected. The second limit defends against slow-sending attacks such as Slowloris, which send just enough data to keep resetting a simple read timeout.
- **Concurrent clients**: each connection is handled on its own thread, up to 100 at once. Further connections are turned away until a slot frees up, so a flood of connections cannot exhaust the server's memory.
- **Keep-alive**: an HTTP/1.1 connection carries up to 100 requests, including several sent back to back in one packet (pipelining), so a browser can load a page and its CSS, JavaScript, and images without reconnecting. The connection closes when the client asks, for HTTP/1.0, after an error, or when a request has a body.
- **Fault isolation**: an unexpected error while handling one client is caught on that client's thread, logged, and answered with `500 Internal Server Error`, while every other connection keeps working.

## Building and running

Requirements: a C++17 compiler and GNU Make. The same Makefile works on both systems and detects which one it is running on.

**Windows.** I use [MSYS2](https://www.msys2.org/) with the UCRT64 toolchain, where Make is called `mingw32-make`.

```
mingw32-make                  # build the server and all test programs
mingw32-make run              # build and run every test program
.\build\windows\server.exe    # start the server
```

**Linux** (tested on Ubuntu under WSL2, with `build-essential` installed):

```
make                          # build the server and all test programs
make run                      # build and run every test program
./build/linux/server          # start the server
```

Each platform builds into its own folder, `build/windows/` or `build/linux/`, so both can be built from the same checkout without overwriting each other. `make clean` deletes the whole `build/` folder.

Start the server from the project folder, then open <http://localhost:8080>. The server serves the demo site in `public/`. Stop it with Ctrl+C. Under WSL2, a server started in Linux can be opened from a browser in Windows at the same address.

The server listens on `127.0.0.1` only, so it can be reached from this computer but not from other machines on the network.

## Design decisions

**Two layers of path traversal protection.** A request like `GET /../secret.txt` must never read a file outside `public/`.

1. Before touching the file system, any path containing `..`, a backslash, a `:` (drive letter), or `//` is rejected. Browsers remove `..` segments before sending a request, so legitimate clients never send them.
2. The resolved path is then made absolute with `std::filesystem::weakly_canonical` and compared component by component (not as a string prefix) with the root folder, so anything that slipped past the first layer still cannot escape.

Blocked paths return `404`, the same response as a file that does not exist, so the server reveals nothing about what is on disk.

**Decode once, then check.** The path is percent-decoded exactly once, after the query string is removed and before either traversal layer runs. Checking before decoding would let `%2e%2e` slip past the `..` filter, and decoding twice would turn `%252e` into `.`, a classic bypass. Encoded slashes are rejected because they would let filters and the file system disagree about where path segments begin, and encoded NUL bytes because some file APIs treat NUL as the end of a string.

**Strict parsing.** Lenient parsers that accept things like whitespace before a header colon or stray carriage returns can disagree with proxies about where a request ends, which is the basis of HTTP request smuggling. This server rejects anything ambiguous. Rejecting all duplicate headers is deliberately stricter than the specification.

**RAII for operating system resources.** `WinsockSession` starts and cleans up Winsock on Windows (Linux needs no setup, so there it does nothing), and `Socket` owns a single socket handle and closes it in its destructor. `Socket` cannot be copied, only moved, so every handle is closed exactly once, even when a function returns early.

**Testable core, thin networking layer.** All decisions live in `handleRequest`, which takes the raw request text and returns a response without touching any sockets. The networking code in `server.cpp` only accepts connections, reads, calls the handler, and sends.

**Return values instead of exceptions.** Expected outcomes such as a missing file or a malformed request are returned as values (`std::optional`, `HttpResponse`) rather than thrown, so every path through the code is visible. Exceptions are reserved for the truly unexpected, such as running out of memory, and each client thread catches them so that one failure cannot terminate the whole server.

**One thread per client, with a counted limit.** The main thread only accepts connections; each client is moved into its own detached thread, which owns the socket and closes it on exit. An atomic counter tracks active clients. The main thread increments it before starting a thread, so a sudden burst of connections cannot slip past the limit, and a small RAII guard in the client thread decrements it on every exit path, including errors.

**Keep-alive only when it is safe.** One function decides whether a connection stays open, after the response is built, so every response states the decision in its `Connection` header and the server acts on that header alone. The connection is closed whenever the server cannot be sure where the next request begins: after a `400`, or when a request carries `Content-Length` or `Transfer-Encoding`. This server never reads request bodies, so an unread body would otherwise be parsed as the next request, which is how request smuggling attacks hide a second request inside the first. Bytes that arrive after the end of one request are kept in the `Socket` and used for the next. The same concern shapes `HEAD`: the body is still built so `Content-Length` is correct, then left out of the output by an `omitBody` flag, because any stray body bytes would be read by the client as the start of the next response.

**A 304 is the 200 with three changes.** A `304 Not Modified` is built from the real file response by changing its status and reason and setting `omitBody`, so it keeps the same `Content-Type`, `Last-Modified`, and `Content-Length` (the size of the cached file, as RFC 9110 requires) without any separate code path. Dates are always formatted and parsed in GMT, and parsing is strict: a date is accepted only if formatting it again reproduces the exact text, which rejects trailing junk, a wrong weekday, or impossible dates like 30 February. An unreadable `If-Modified-Since` is ignored, so the worst case is sending the file in full.

**Small, contained platform differences.** Windows and Linux differ in socket types, error codes, timeout formats, and date functions, but each difference is handled in one small `#ifdef _WIN32` block that turns the platform's result into a shared name or value (`SocketHandle`, `lastSocketError()`, a `converted` flag), so the logic around it is written once. Two differences change behavior, not just names:

- On Linux, writing to a connection the client has already closed raises `SIGPIPE`, which terminates the process by default. Every send passes `MSG_NOSIGNAL`, so a client hanging up mid-download becomes an ordinary send error that ends only that connection.
- After a restart, Linux keeps the previous run's closed connections in `TIME-WAIT` for 60 seconds and refuses to bind the port until they expire. The server sets `SO_REUSEADDR` on Linux so it can restart immediately. It is deliberately not set on Windows, where the same option means something different: it would let another program bind the port while the server is still running.

## Tests

Nine test programs with 328 assertions cover request parsing, response formatting, error pages, content types, percent-decoding, keep-alive decisions, `HEAD` requests, HTTP dates and conditional requests, and file serving, including fifteen path traversal attacks, seven of them percent-encoded. Run them with `mingw32-make run` on Windows or `make run` on Linux; all of them pass on both. GitHub Actions builds the project and runs every test on fresh Windows and Ubuntu machines after each push, so a change that breaks either platform shows up right away.

`test_public/` contains small files the tests serve. `test_secret.txt` sits outside that folder on purpose: it is a fake "secret" that the traversal tests try to reach, to prove they cannot. It contains no real data.

## Project structure

| File | Purpose |
|---|---|
| `http.h` / `http.cpp` | Request parsing, response building, content types, path resolution, and `handleRequest` |
| `socket.h` / `socket.cpp` | The Windows and Linux socket differences, `WinsockSession`, and the move-only `Socket` class, which buffers bytes left over from one request for the next |
| `server.cpp` | `main`, which accepts connections, and `handleClient`, which reads, handles, and sends each request on the client's own thread |
| `*_test.cpp` | Test programs, found and run automatically by the Makefile |
| `.github/workflows/ci.yml` | The GitHub Actions workflow that builds and tests on Windows and Linux |
| `public/` | The demo site the server serves |
| `test_public/`, `test_secret.txt` | Fixtures for the file-serving and traversal tests |

## Limitations

This is a learning project, not a production server.

- An idle keep-alive connection holds one of the 100 client slots until it times out after 5 seconds. Browsers open up to six connections per site, so about 16 visitors can be connected at the same moment.
- It uses one thread per client rather than a thread pool or asynchronous I/O, which is simple but would not scale to many thousands of connections.
- Only `GET` and `HEAD` are supported; other methods get `405 Method Not Allowed`.
- Caching uses dates only: there are no `ETag` validators, and `If-Modified-Since` is accepted only in the standard date format, not the two obsolete formats RFC 9110 also allows.
- It has been tested on Windows and on Linux (Ubuntu), but not on macOS.

## About this project

I built this project step by step to learn networking and C++, using Claude as a tutor and code reviewer. The design decisions and implementation are my own.
