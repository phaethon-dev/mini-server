# mini-server

A small HTTP/1.1 static file server written in C++17 from raw Windows sockets up, with no networking or HTTP libraries. It parses requests strictly against the HTTP specification, serves files from a folder with the correct content types, and blocks attempts to read files outside that folder.

## Features

- **Strict request parsing** that follows RFC 9112: exactly three request-line parts separated by single spaces, a version matching `HTTP/<digit>.<digit>`, `\r\n` line endings, and a required blank line after the headers.
- **Header validation**: names may contain only letters, digits, and `-`; only spaces and tabs are trimmed from values; control characters and duplicate headers are rejected.
- **Static file serving** from a `public` folder, sent byte for byte so images and other binary files arrive intact.
- **Folder support**: requesting a folder serves its `index.html`.
- **Content types** chosen from the file extension, case-insensitively: HTML, CSS, JavaScript, JSON, PNG, JPEG, GIF, plain text, and PDF, with `application/octet-stream` as the fallback.
- **Percent-decoding** of request paths, so `/my%20photo.jpg` serves `my photo.jpg`. Malformed escapes such as `%zz`, an encoded NUL byte (`%00`), and an encoded slash (`%2F`) are rejected with `400 Bad Request`.
- **Path traversal protection** in two independent layers (see below).
- **Clear error responses**: `400 Bad Request`, `404 Not Found`, `405 Method Not Allowed` (with the required `Allow` header), and `500 Internal Server Error`, each with a small HTML page.
- **Robust networking**: requests that arrive in several pieces are reassembled, responses are sent completely even when the socket accepts only part at a time, and request headers are limited to 8 KB.
- **Timeouts**: a client that goes silent for 5 seconds, or takes longer than 10 seconds to send its whole request, is disconnected. The second limit defends against slow-sending attacks such as Slowloris, which send just enough data to keep resetting a simple read timeout.
- **Concurrent clients**: each connection is handled on its own thread, up to 100 at once. Further connections are turned away until a slot frees up, so a flood of connections cannot exhaust the server's memory.
- **Fault isolation**: an unexpected error while handling one client is caught on that client's thread, logged, and answered with `500 Internal Server Error`, while every other connection keeps working.

## Building and running

Requirements: Windows, a C++17 compiler, and GNU Make. I use [MSYS2](https://www.msys2.org/) with the UCRT64 toolchain, where Make is called `mingw32-make`.

```
mingw32-make            # build the server and all test programs
mingw32-make run        # build and run every test program
.\server.exe            # start the server
```

Then open <http://localhost:8080>. The server serves the demo site in `public/`. Stop it with Ctrl+C.

The server listens on `127.0.0.1` only, so it can be reached from this computer but not from other machines on the network.

## Design decisions

**Two layers of path traversal protection.** A request like `GET /../secret.txt` must never read a file outside `public/`.

1. Before touching the file system, any path containing `..`, a backslash, a `:` (drive letter), or `//` is rejected. Browsers remove `..` segments before sending a request, so legitimate clients never send them.
2. The resolved path is then made absolute with `std::filesystem::weakly_canonical` and compared component by component (not as a string prefix) with the root folder, so anything that slipped past the first layer still cannot escape.

Blocked paths return `404`, the same response as a file that does not exist, so the server reveals nothing about what is on disk.

**Decode once, then check.** The path is percent-decoded exactly once, after the query string is removed and before either traversal layer runs. Checking before decoding would let `%2e%2e` slip past the `..` filter, and decoding twice would turn `%252e` into `.`, a classic bypass. Encoded slashes are rejected because they would let filters and the file system disagree about where path segments begin, and encoded NUL bytes because some file APIs treat NUL as the end of a string.

**Strict parsing.** Lenient parsers that accept things like whitespace before a header colon or stray carriage returns can disagree with proxies about where a request ends, which is the basis of HTTP request smuggling. This server rejects anything ambiguous. Rejecting all duplicate headers is deliberately stricter than the specification.

**RAII for operating system resources.** `WinsockSession` starts and cleans up Winsock, and `Socket` owns a single socket handle and closes it in its destructor. `Socket` cannot be copied, only moved, so every handle is closed exactly once, even when a function returns early.

**Testable core, thin networking layer.** All decisions live in `handleRequest`, which takes the raw request text and returns a response without touching any sockets. The networking code in `server.cpp` only accepts connections, reads, calls the handler, and sends.

**Return values instead of exceptions.** Expected outcomes such as a missing file or a malformed request are returned as values (`std::optional`, `HttpResponse`) rather than thrown, so every path through the code is visible. Exceptions are reserved for the truly unexpected, such as running out of memory, and each client thread catches them so that one failure cannot terminate the whole server.

**One thread per client, with a counted limit.** The main thread only accepts connections; each client is moved into its own detached thread, which owns the socket and closes it on exit. An atomic counter tracks active clients. The main thread increments it before starting a thread, so a sudden burst of connections cannot slip past the limit, and a small RAII guard in the client thread decrements it on every exit path, including errors.

## Tests

Six test programs with 215 assertions cover request parsing, response formatting, error pages, content types, percent-decoding, and file serving, including fifteen path traversal attacks, seven of them percent-encoded. Run them with `mingw32-make run`.

`test_public/` contains small files the tests serve. `test_secret.txt` sits outside that folder on purpose: it is a fake "secret" that the traversal tests try to reach, to prove they cannot. It contains no real data.

## Project structure

| File | Purpose |
|---|---|
| `http.h` / `http.cpp` | Request parsing, response building, content types, path resolution, and `handleRequest` |
| `socket.h` / `socket.cpp` | `WinsockSession` and the move-only `Socket` class |
| `server.cpp` | `main`, which accepts connections, and `handleClient`, which reads, handles, and sends on each client's thread |
| `*_test.cpp` | Test programs, found and run automatically by the Makefile |
| `public/` | The demo site the server serves |
| `test_public/`, `test_secret.txt` | Fixtures for the file-serving and traversal tests |

## Limitations

This is a learning project, not a production server.

- It handles one request per connection (`Connection: close`).
- It uses one thread per client rather than a thread pool or asynchronous I/O, which is simple but would not scale to many thousands of connections.
- Only `GET` is supported.
- It runs on Windows only, because it uses Winsock directly.

## About this project

I built this project step by step to learn networking and C++, using Claude as a tutor and code reviewer. The design decisions and implementation are my own.
