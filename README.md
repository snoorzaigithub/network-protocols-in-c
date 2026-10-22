# Network Protocols in C

A collection of C networking projects exploring application-layer protocols, socket programming, and reliable data transmission. This repository documents implementations developed across seven projects, progressing from HTTP clients and servers to a custom Reliable UDP (RUDP) transport protocol.

## Overview

The projects explore how network applications communicate, how protocols structure requests and responses, and how reliable delivery can be implemented on top of UDP.

**Technologies:** C, POSIX sockets, TCP/IP, UDP, HTTP/1.1, SMTP, DNS/address resolution, and SCons.

## Projects

### Project 1 — HTTP Client

Implemented a command-line HTTP client that connects to a specified host and port, constructs HTTP/1.1 requests, sends the required headers, receives server responses, and prints response headers and body data.

**Key concepts**
- HTTP request structure and headers
- TCP connection management
- Sending and receiving network packets
- Parsing response metadata and content length

### Project 2 — HTTP Server

Implemented a minimal HTTP server that accepts a client connection, processes a GET request, reads the requested file, and returns an HTTP/1.1 response.

**Key concepts**
- HTTP request handling and response construction
- File operations in C
- Content-Length and Content-Type headers
- Handling missing files with a 404 response

### Project 3 — SMTP Agent

Developed an SMTP agent to communicate with a mail server and send email using the Simple Mail Transfer Protocol.

**Key concepts**
- SMTP protocol commands and responses
- Stateful client-server communication
- Email message transmission
- Working with a restricted course testing environment

### Project 4 — Sockets and DNS

Implemented socket connection-management functionality using address resolution and TCP sockets, including establishing outgoing connections and accepting incoming connections.

**Key concepts**
- Address resolution with `getaddrinfo`
- Socket creation and connection establishment
- Server binding and listening
- Managing connection lifecycle

### Project 5 — Reliable UDP Handshake

Started implementing a custom connection-oriented transport protocol using UDP datagrams and a handshake to establish communication between endpoints.

**Key concepts**
- UDP socket programming
- Custom transport protocol design
- Connection establishment and handshake logic
- Building transport-layer functionality above UDP

### Project 6 — Stop-and-Wait Reliability

Implemented the initial reliability mechanisms for the custom transport protocol, including packet handling, acknowledgements, and retransmission behavior.

**Key concepts**
- Packet sequencing and acknowledgements
- Timeout-based retransmission
- Send-window management
- Reliable delivery over UDP

### Project 7 — Go-Back-N and Backend Driver

Extended the transport implementation with a backend responsible for sending pending packets, processing acknowledgements, retransmitting outstanding packets after timeouts, and managing a larger send window.

**Key concepts**
- Go-Back-N ARQ
- Sliding-window protocols
- Circular buffer design
- Concurrent packet processing and synchronization

## Build

The projects use SCons as the build system in the provided course environment.

```bash
scons build
```

To check out a particular project from the original course repository, the course instructions use:

```bash
scons checkout project=1
```

Replace `1` with the appropriate project number. Checkout is only needed when retrieving the corresponding course source files.

## Learning Outcomes

Through these projects, I explored:

- Implementing application-layer protocols in C
- Understanding HTTP and SMTP communication
- Working with TCP sockets and UDP datagrams
- Resolving network addresses and managing connections
- Designing reliable delivery mechanisms over an unreliable transport
- Implementing acknowledgement, timeout, and retransmission logic
- Understanding sliding-window protocols and packet buffering
- Working with low-level networking APIs and constrained C implementations

## Repository Structure

The source layout depends on the course-provided project files. The main implementation files include:

```text
src/
├── http_client.c
├── http_server.c
├── sans_socket.c
├── sans_transport.c
└── ...
```

Additional files may include the SMTP agent, transport backend, headers, and build configuration.

## Notes

These projects were developed in an educational networking environment and use course-provided networking interfaces. Some implementations may require the original course environment and support files to build or run.

This repository is intended to document my learning and implementation work in network programming, socket APIs, and reliable transport protocols.
