# mini-server

A small HTTP/1.1 static file server written in C++17 from raw Windows sockets up, with no networking or HTTP libraries. It parses requests strictly against the HTTP specification, serves files from a folder with the correct content types, and blocks attempts to read files outside that folder.

## Features

- **Strict request parsing** that follows RFC 9112: exactly three request-line parts separated by single spaces, a version matching `HTTP/<digit>.<digit>`, `\r\n` line endings, and a required blank line after the headers.
- **Header validation**: names may contain only letters, digits, and `-`; only spaces and tabs are trimmed from values; control characters and duplicate headers are rejected.
- **Static file serving** from a `public` folder, sent byte for byte so images and other binary files arrive intact.
- **Folder support**: requesting a folder serves its `index.html`.
- **Content types** chosen from the file extension, case-insensitively: HTML, CSS, JavaScript, JSON, PNG, JPEG, GIF, plain text, and PDF, with `application/octet-stream` as the fallback.
- **Path traversal protection** in two independent layers (see below).
- **Clear error responses**: `400 Bad Request`, `404 Not Found`, and `405 Method Not Allowed` (with the required `Allow` header), each with a small HTML page.
- **Robust networking**: requests that arrive in several pieces are reassembled, responses are sent completely even when the socket accepts only part at a time, and request headers are limited to 8 KB.

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

**Strict parsing.** Lenient parsers that accept things like whitespace before a header colon or stray carriage returns can disagree with proxies about where a request ends, which is the basis of HTTP request smuggling. This server rejects anything ambiguous. Rejecting all duplicate headers is deliberately stricter than the specification.

**RAII for operating system resources.** `WinsockSession` starts and cleans up Winsock, and `Socket` owns a single socket handle and closes it in its destructor. `Socket` cannot be copied, only moved, so every handle is closed exactly once, even when a function returns early.

**Testable core, thin networking layer.** All decisions live in `handleRequest`, which takes the raw request text and returns a response without touching any sockets. The networking code in `server.cpp` only accepts connections, reads, calls the handler, and sends.

**Return values instead of exceptions.** Expected outcomes such as a missing file or a malformed request are returned as values (`std::optional`, `HttpResponse`) rather than thrown, so every path through the code is visible.

## Tests

Five test programs with 160 assertions cover request parsing, response formatting, error pages, content types, and file serving, including eight path traversal attacks. Run them with `mingw32-make run`.

`test_public/` contains small files the tests serve. `test_secret.txt` sits outside that folder on purpose: it is a fake "secret" that the traversal tests try to reach, to prove they cannot. It contains no real data.

## Project structure

| File | Purpose |
|---|---|
| `http.h` / `http.cpp` | Request parsing, response building, content types, path resolution, and `handleRequest` |
| `socket.h` / `socket.cpp` | `WinsockSession` and the move-only `Socket` class |
| `server.cpp` | `main`: the accept, read, handle, send loop |
| `*_test.cpp` | Test programs, found and run automatically by the Makefile |
| `public/` | The demo site the server serves |
| `test_public/`, `test_secret.txt` | Fixtures for the file-serving and traversal tests |

## Limitations

This is a learning project, not a production server.

- It handles one connection at a time, and one request per connection (`Connection: close`).
- There is no read timeout, so a client that connects and sends nothing holds up the server until it disconnects.
- Only `GET` is supported.
- Percent-encoded paths (such as `%20`) are not decoded.
- It runs on Windows only, because it uses Winsock directly.

## About this project

I built this project step by step to learn networking and C++, using Claude as a tutor and code reviewer. The design decisions and implementation are my own.
