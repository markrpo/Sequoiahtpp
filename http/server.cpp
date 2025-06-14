#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cassert>

#include <vector>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
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

enum {
	STATE_READ,
	STATE_WRITE,
	STATE_CLOSE
};

const size_t MAX_BUF_SIZE = 32 << 20; // 32 MB
const size_t K_MAX_STRINGS = 1024;

void die(const char* msg) {
	perror(msg);
	exit(1);
}

void set_nonblock(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) { die("fcntl(F_GETFL)"); }
	int rv = fcntl(fd, F_SETFL, flags | O_NONBLOCK); // Set the file descriptor to non-blocking mode, this means that read and write operations will not block the process
													 // this means that if there is no data to read, read will return -1 and set errno to EAGAIN
	if (rv == -1) { die("fcntl(F_SETFL)"); }
}

struct Conn{
		int fd = -1;
		uint8_t state = STATE_READ;
		size_t read_size = 0;
		size_t find_pos = 0;
		int8_t found_number = 0;
		uint8_t read_buf[MAX_BUF_SIZE];
		size_t write_size = 0;
		uint8_t write_buf[MAX_BUF_SIZE];
};

Conn* handle_accept(int fd) {
	struct sockaddr_in addr = {};
	socklen_t addrlen = sizeof(addr);
	int client_fd = accept(fd, (struct sockaddr*)&addr, &addrlen);
	printf("Accepted connection from %s:%d, fd: %i\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), client_fd);
	if (client_fd < 0) { die("accept"); }
	set_nonblock(client_fd);

	Conn* conn = new Conn();																					/* 	Sometimes this is necessary to allocate memory in the heap, 
																													also when we exit the scope of the function, the memory is not deallocated so
																													releasing the memory is our responsibility, and we can return the pointer to the memory */
	conn->fd = client_fd;
	conn->state = STATE_READ;
	return conn;
}

// check if the request ends with \r\n\r\n, if it does, we have a complete request
bool parse_request(Conn* conn) {
	printf("Parsing request, read size: %i\n", (int)conn->read_size);
	printf("Read buffer: %.*s\n", (int)conn->read_size, (const char*)conn->read_buf);	// 	Print the read buffer as a string, up to the read size
	if (conn->read_size < 4) { return false; }																// 	If the read buffer is less than 4 bytes, we don't have a complete request yet
	while (conn->find_pos != conn->read_size && conn->found_number != 4)
	{
		switch (conn->found_number) {
			case 0: // looking for \r
				if (conn->read_buf[conn->find_pos] == '\r') {
					conn->found_number = 1;
				}
				conn->find_pos++;
				break;
			case 1: // looking for \n
				if (conn->read_buf[conn->find_pos] == '\n') {
					conn->found_number = 2;
				} else {
					conn->found_number = 0; // reset to search for \r again
				}
				conn->find_pos++;
				break;
			case 2: // looking for \r
				if (conn->read_buf[conn->find_pos] == '\r') {
					conn->found_number = 3;
				} else {
					conn->found_number = 0; // reset to search for \r again
				}
				conn->find_pos++;
				break;
			case 3: // looking for \n
				if (conn->read_buf[conn->find_pos] == '\n') {
					printf("Found end of request in position %i\n", (int)conn->find_pos);
					printf("Headers: %.*s\n", (int)(conn->find_pos + 1), (const char*)conn->read_buf);
					conn->found_number = 4; 
				} else {
					conn->found_number = 0; // reset to search for \r again
				}
				conn->find_pos++;
				break;
			case 4: 
				break;
			default:
				assert(false); // should never happen
		}
	}
	printf("HTTP request received, size: %i\n", (int)conn->read_size);
	printf("Body: %.*s\n", (int)(conn->read_size - conn->find_pos), (const char*)(conn->read_buf + conn->find_pos));
	conn->read_size = 0;
	//memmove(&conn->read_buf[0], &conn->read_buf[conn->read_size], conn->find_pos);		// 	Move the remaining data in the read buffer to the start of the buffer
	
	return true; // complete request, we can process it return true to continue processing
}

bool handle_write(Conn* conn) {
	assert(conn->write_size > 0);																				// 	Assert is used to check if a condition is true, if it is not, the program will terminate
	int32_t len;
	memcpy(&len, &conn->write_buf[0], 4);																		// 	Copy 4 bytes from the write buffer to len
	uint8_t* data = &conn->write_buf[4];																		// 	Data points to the start of the message (without the length)
	printf("Len: %i Sending data: %.*s\n",len, (int)len < 10 ? (int)len : 10, (const char*)data);
	
	ssize_t rv = write(conn->fd, &conn->write_buf[0], conn->write_size);
	if (rv < 0 && errno == EAGAIN) {																			// 	EAGAIN means that the write would block, so we should try again later
		printf("returning EAGAIN");																				// 	This is necessary because pipelined requests can cause the write buffer to fill up (and EAGAIN to be returned)
		return false;
	}
	if (rv < 0) { conn->state = STATE_CLOSE; printf("rv < 0 Closing"); return false; }
	printf("Wrote %i bytes, remaining (write buff): %i\n", (int)rv, (int)(conn->write_size - rv));
	size_t remain = conn->write_size - rv;
	if (remain > 0)	{
	memmove(&conn->write_buf[0], &conn->write_buf[rv], remain);													// 	Move remaining data from write buffer [rv] position to the start of the buffer [0]
	}
	conn->write_size = remain;
	if (conn->write_size == 0) {
		conn->state = STATE_READ;
		printf("Switching to read ALL in writte buffer writted\n");
		return false;
	}
	return true;
}

bool handle_read(Conn* conn) {
	printf("Readin from %i\n", conn->fd);
	ssize_t rv = read(conn->fd, conn->read_buf + conn->read_size, MAX_BUF_SIZE - conn->read_size);		// 	Read data from the connection into the read buffer, starting at the end of the current read size

	if (rv < 0 && errno == EAGAIN) {
		printf("returning EAGAIN (read) this means that there is no data to read at the moment, so we should try again later\n");
		return false;
	}
	if (rv < 0) {
		perror("read error");
		conn->state = STATE_CLOSE;
		return false;
	}
	if (rv == 0) {
		printf("EOF\n");
		conn->state = STATE_CLOSE;
		return false;
	}
	if (conn->read_size + rv > MAX_BUF_SIZE) {
		printf("Read buffer overflow, closing connection\n");
		conn->state = STATE_CLOSE;
		return false;
	}

	printf("Read %i bytes\n", (int)rv);
	conn->read_size += rv;																						// 	Increase the size of the read buffer
	printf("Read size: %i\n", (int)conn->read_size);
	assert(conn->read_size <= sizeof(conn->read_buf));
	while(parse_request(conn)){ }																				// 	See if we have a complete request (will stay in the loop until we don't have a complete request)
	
	return true;
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
		poll_args.clear();
		struct pollfd pfd = {fd, POLLIN, 0};
		poll_args.push_back(pfd);
		
		for ( Conn* conn : conns ) {
			if (!conn) { continue; }
			struct pollfd pfd = {conn->fd, POLLERR, 0}; // 	Create a pollfd structure for each connection, POLLERR is used to check for errors on the connection
			if (conn->state == STATE_READ) { pfd.events |= POLLIN; }	// 	If the connection is in STATE_READ, add POLLIN to the events to check for data to read
																		// 	|= is a bitwise OR operator, it sets the bits in pfd.events to the bits in POLLIN
			if (conn->state == STATE_WRITE) { pfd.events |= POLLOUT; }
			poll_args.push_back(pfd);
		}
		int rv = 0;
		while (rv == 0) {
			rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000); // pass the vector of pollfd structures to poll to wait for events
																		 // the timeout is set to 1000 milliseconds (1 second), so poll will wait for events for 1 second
																		 // if no events occur, poll will return 0
																		 // if the timeout is -1, poll will wait indefinitely for events
			printf("Poll returned %i, size: %zu\n", rv, poll_args.size());
		}
		if (rv < 0) { die("poll"); }

		// if the server socket has an event, handle it, .revents is a bitmask of events that have occurred
		if (poll_args[0].revents) {
			if (Conn* conn = handle_accept(fd)) {															// 	If the handle_accept function returns a pointer to a Conn object, add it to the conns vector
				if (conns.size() <= (size_t)conn->fd) {														// 	If the total size of the vector is less than the file descriptor of the new connection, resize the vector
					conns.resize(conn->fd + 1);																// 	to at least have the size of the file descriptor of the new connection example= conns[5] means we have 6 connections
																											// 	if the fd of the new connection is 5, we need to resize the vector to have at least 6 elements
				}
				conns[conn->fd] = conn;																		// 	Add the new connection to conns vector at the index of the file descriptor
			}
		}

		// For each connection in conns, handle read and write events
		for (size_t i = 1; i < poll_args.size(); i++) {
			uint32_t ready = poll_args[i].revents;
			Conn* conn = conns[poll_args[i].fd];															// 	pointer to Conn object in the vector
			if (ready & POLLIN) { 																			/* 	The & operator can be used to check if a bit is set in a bitmask
																 												example: ready = 00000011, POLLIN = 00000001, ready & POLLIN = 00000001 != 0 
																												so we enter the if */
				printf("Reading on %i\n", conn->fd);
				handle_read(conn);																			// 	handle_read will read data from the connection and store it in the read buffer
				printf("Exit handle_read, state: %i\n", conn->state);
			}
			if (ready & POLLOUT) {
				printf("Writting on %i\n", conn->fd);
				handle_write(conn);
			}
			if (ready & POLLERR || conn->state == STATE_CLOSE) { 
				printf("Closed on %i\n", conn->fd);
				(void)close(conn->fd);
				conns[conn->fd] = NULL;
				delete conn;
			}
		}
	}
}

