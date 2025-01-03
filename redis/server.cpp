#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <poll.h>																/*	Poll is used to wait for some event on a file descriptor structure:
				 															 		int poll(struct pollfd fds[], nfds_t nfds, int timeout)
				 															 		fds[] - array of pollfd structures, nfds - number of structures in the array, timeout - time to wait in milliseconds
				 															 		when timeout is -1, poll waits indefinitely for an event
				 															 		struct pollfd {
				 															 		int fd; // file descriptor
				 															 		short events; // requested events
				 															 		short revents; // returned events
				 															 		}
				 															 		events: POLLIN - there is data to read, POLLOUT - ready to write, POLLERR - error condition, POLLHUP - hang up
				 															 		Other API: select (only 1024 fds), epoll (Linux only)	*/

																				/* 	When a socket is ready to read, it means that the data is in the read buffer, 
 																					so the read is guaranteed not to block, but for a disk file, no such buffer exists in 
																					the kernel, so the readiness for a disk file is undefined. */
#include "utils.hpp"

struct Conn{
		int fd = -1;
		bool want_read = false;													/* 	We use bool instead of int because we only need two states: true or false. */
		bool want_write = false;
		bool want_close = false;
		std::vector<uint8_t> read_buf;											/* 	We use uint8_t instead of char because char is signed by default, 
																					so it can be interpreted as a negative number, which is not what we want. */
		std::vector<uint8_t> write_buf;
	};

Conn* handle_accept(int fd) {
	struct sockaddr_in addr = {};
	socklen_t addrlen = sizeof(addr);
	int client_fd = accept(fd, (struct sockaddr*)&addr, &addrlen);
	printf("Accepted connection from %s:%d, fd: %i\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), client_fd);
	if (client_fd < 0) { die("accept"); }
	set_nonblock(client_fd);

	Conn* conn = new Conn();
	conn->fd = client_fd;
	conn->want_read = true;
	return conn;
}

void buffer_append(std::vector<uint8_t>* buf, const uint8_t* buff, ssize_t len) {
	buf->insert(buf->end(), buff, buff + len); 		// The insert method inserts elements into the vector at the specified position
													// buf->end() returns an iterator to the end of the vector
													// buff + len is a pointer to the end of the buffer
	

}

void buf_consume(std::vector<uint8_t>& buf, ssize_t len) {
	buf.erase(buf.begin(), buf.begin() + len);
}

bool parse_request (Conn* conn){
	if (conn->read_buf.size() < 4) { return false; }
	uint32_t len = 0; 
	memcpy(&len, conn->read_buf.data(), 4);		// Copy 4 bytes from the read buffer to len
												// Could also use uint32_t len = *(uint32_t*)conn->read_buf.data(); (uint32_t size is 4 bytes)
	if (conn->read_buf.size() < 4 + len) { return false; }	// If the buffer is smaller than 4 + len, we don't have a complete message
	const uint8_t* data = conn->read_buf.data() + 4;		// data points to the start of the message (without the length)
	printf("Received request: %.*s\n", (int)len, data);		// Print the message
	
	// Logic goes here (when a message is received)
	buffer_append(&conn->write_buf, (const uint8_t*)&len, 4); // Append the length of the message to the write buffer
	buffer_append(&conn->write_buf, data, len);				// Append the message to the write buffer (echo)

	buf_consume(conn->read_buf, 4 + len);					// Remove the message from the read buffer
	return true;
}


void handle_read(Conn* conn) {
	uint8_t buf[64 * 1024];
	ssize_t rv = read(conn->fd, buf, sizeof(buf));
	if (rv <= 0) {
		conn->want_close = true;
	}
	buffer_append(&conn->read_buf, buf, rv);		// we can also pass y reference to avoid copying the vector
													// see buff_consume function (it uses reference)
	parse_request(conn);							// See if we have a complete request, if so, parse it, process it and remove it from the buffer
	if (conn->write_buf.size() > 0) {
		conn->want_read = false;
		conn->want_write = true;
	}
}

void handle_write(Conn* conn) {
	ssize_t rv = write(conn->fd, conn->write_buf.data(), conn->write_buf.size());
	if (rv < 0) {
		conn->want_close = true;
		return;
	}
	buf_consume(conn->write_buf, rv);				// Remove the written data from the write buffer
	if (conn->write_buf.size() == 0) {
		conn->want_write = false;
		conn->want_read = true;
	}
}

int main () {
	
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); 	
	struct sockaddr_in addr = {};

	set_nonblock(fd);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(1234);
	addr.sin_addr.s_addr = ntohl(0); 
	int rv = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (rv) { die("bind"); } 			
	
	rv = listen(fd, 10); 			
	if (rv) { die("listen"); }

	std::vector<Conn*> conns;
	std::vector<pollfd> poll_args;

	while(true) {
		// Clear poll_args (size = 0) and add server socket to it
		poll_args.clear();
		// Add server socket to poll_args
		struct pollfd pfd = {fd, POLLIN, 0};
		poll_args.push_back(pfd);
		
		// For each connection in conns, add it to poll_args
		// if it wants to read or write
		for ( Conn* conn : conns ) {
			if (!conn) { continue; }
			struct pollfd pfd = {conn->fd, POLLERR, 0};
			if (conn->want_read) { pfd.events |= POLLIN; }		// because conn is a pointer to a sctruct (Conn*) we use -> instead of . to access its members
			if (conn->want_write) { pfd.events |= POLLOUT; }
			poll_args.push_back(pfd);
		}
		/* 	.data() returns a pointer to the pollfd array (poll_args) is the same as &poll_args[0], nfds_t type is a typedef for unsigned 
			int that is used to represent the number of file descriptors in the pollfd array -1 is the timeout, which means that poll will wait 
			indefinitely for an event, poll returns the number of file descriptors that have events, or -1 if there is an error. */
		int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1); 
		if (rv < 0) { die("poll"); }

		// if the server socket has an event, handle it, .revents is a bitmask of events that have occurred
		if (poll_args[0].revents) {
			if (Conn* conn = handle_accept(fd)) {
				if (conns.size() <= (size_t)conn->fd) {			// If the total size of the vector is less than the file descriptor of the new connection, resize the vector
					conns.resize(conn->fd + 1);					// to at least have the size of the file descriptor of the new connection example= conns[5] means we have 6 connections
																// if the fd of the new connection is 5, we need to resize the vector to have at least 6 elements
				}
				conns[conn->fd] = conn;							// Add the new connection to conns vector at the index of the file descriptor
			}
		}

		// For each connection in conns, handle read and write events
		for (size_t i = 1; i < poll_args.size(); i++) {
			uint32_t ready = poll_args[i].revents;
			Conn* conn = conns[poll_args[i].fd];				// pointer to Conn object in the vector
			if (ready & POLLIN) { 								/* 	The & operator can be used to check if a bit is set in a bitmask
																 	example: ready = 00000011, POLLIN = 00000001, ready & POLLIN = 00000001 != 0 
																	so we enter the if */
				handle_read(conn);								// handle_read will read data from the connection and store it in the read buffer
			}
			if (ready & POLLOUT) {
				handle_write(conn);
			}
			if (ready & POLLERR) {
				(void)close(conn->fd);
				conns[conn->fd] = NULL;
				delete conn;
			}
		}


	}
}

