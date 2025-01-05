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

enum {
	STATE_READ,
	STATE_WRITE,
	STATE_CLOSE
};

enum {
	RES_OK,
	RES_ERR,
	RES_NX 
};

struct Conn{
		int fd = -1;
		uint8_t state = STATE_READ;
		std::vector<uint8_t> read_buf;																			/* 	We use uint8_t instead of char because char is signed by default, 
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
	conn->state = STATE_READ;
	return conn;
}

void buffer_append(std::vector<uint8_t>* buf, const uint8_t* buff, ssize_t len) {
	buf->insert(buf->end(), buff, buff + len); 																	/* 	The insert method inserts elements into the vector at the specified position
																												 	in this case, we insert the elements at the end of buf
																												 	and the data do copy is going to be the data from buff to buff + len (memory range) */
}

void buf_consume(std::vector<uint8_t>& buf, ssize_t len) {
	//printf("Buffer size: %i\n", (int)buf.size());
	//printf("Consuming %i bytes\n", (int)len);
	buf.erase(buf.begin(), buf.begin() + len);
	//printf("New buffer size: %i\n", (int)buf.size());
}

int32_t do_request(const uint8_t* data, Conn* conn, uint32_t len, uint32_t* rescode, uint32_t* wlen) {
	// Logic goes here (when a message is received)
	*rescode = RES_OK;
	*wlen = len;
	printf("Copied %i bytes\n", (int)len);
	buffer_append(&conn->write_buf, (const uint8_t*)&len, 4); 
	buffer_append(&conn->write_buf, data, len); 
	return 0;
}

//int32_t do_request(const uint8_t* data, uint8_t* res, uint32_t len, uint32_t* rescode, uint32_t* wlen) {
	// Logic goes here (when a message is received)
//	*rescode = RES_OK;
//	*wlen = len;
//	memcpy(res, data, len);
//	return 0;
//} 

bool handle_write(Conn* conn) {
	assert(conn->write_buf.size() > 0);																			// 	Assert is used to check if a condition is true, if it is not, the program will terminate
	int32_t len;
	memcpy(&len, conn->write_buf.data(), 4);																	// 	Copy 4 bytes from the write buffer to len
	uint8_t* data = conn->write_buf.data() + 4;																	// 	Data points to the start of the message (without the length)
	printf("Len: %i Sending data: %.*s\n",len, (int)len < 10 ? (int)len : 10, (const char*)data);
	
	ssize_t rv = write(conn->fd, conn->write_buf.data(), conn->write_buf.size());
	if (rv < 0 && errno == EAGAIN) {																			// 	EAGAIN means that the write would block, so we should try again later
		printf("returning EAGAIN");																				// 	This is necessary because pipelined requests can cause the write buffer to fill up (and EAGAIN to be returned)
		return false;
	}
	if (rv < 0) { conn->state = STATE_CLOSE; printf("rv < 0 Closing"); return false; }
	printf("Wrote %i bytes\n", (int)rv);
	buf_consume(conn->write_buf, rv);				
	if (conn->write_buf.size() == 0) {
		conn->state = STATE_READ;
		printf("Switching to read ALL in writte buffer writted\n");
		return false;
	}
	printf ("Still data to write\n");
	return true;
}

bool parse_request (Conn* conn){
	if (conn->read_buf.size() < 4) { return false; }
	uint32_t len = 0; 
	memcpy(&len, conn->read_buf.data(), 4);																		// 	Copy 4 bytes from the read buffer to len
																												// 	Could also use uint32_t len = *(uint32_t*)conn->read_buf.data(); (uint32_t size is 4 bytes)
	if (conn->read_buf.size() < 4 + len) { return false; }														// 	If the buffer is smaller than 4 + len, we don't have a complete message
	const uint8_t* data = conn->read_buf.data() + 4;															// 	Data points to the start of the message (without the length)

	printf("Received of length: %i\n", (int)len);																// 	Print the length of the message and print part of the message;
	printf("Message: %.*s\n", (int)len < 10 ? (int)len : 10, (const char*)data);								// 	%.*s is a format specifier that takes two arguments, the first is the length of the string and the second is the string
																												// 	if the length is less than 10, print the whole string, otherwise print the first 10 characters
	
	uint32_t rescode = 0;
	uint32_t wlen = 0;
	int32_t err = do_request(data, conn, len, &rescode, &wlen);
	//int32_t err = do_request(data, conn->write_buf.data() + (4+4), len, &rescode, &wlen);	
	if (err) { conn->state = STATE_CLOSE; return false; }

	wlen += 4;

														
	
	// Logic goes here (when a message is received)
	// buffer append recieves the buffer, starting address of the data and the length of the data we want to append
	//buffer_append(&conn->write_buf, (const uint8_t*)&wlen, 4); // Append the length of the message to the write buffer
	//buffer_append(&conn->write_buf, (const uint8_t*)&rescode, 4); // Append the result code to the write buffer

	// Remove the message from the read buffer 
	// buf_consume recieves the buffer and the length of the data we want to remove
	buf_consume(conn->read_buf, 4 + len);					
	conn->state = STATE_WRITE;								
	while (handle_write(conn)) { 
	}																											// 	Write until we send all the data (or we get EAGAIN (kernel buffer full))
	printf ("handel_write returned false\n");
	return true;																								// 	Return true if message was sended
}

bool handle_read(Conn* conn) {
	uint8_t buf[64 * 1024];
	ssize_t rv = read(conn->fd, buf, sizeof(buf));
	if (rv <= 0) {
		conn->state = STATE_CLOSE;
		return false;
	}
	buffer_append(&conn->read_buf, buf, rv);																	// 	We can also pass y reference to avoid copying the vector see buffer_append function
	while(parse_request(conn)){ }																				// 	See if we have a complete request (will stay in the loop until we don't have a complete request)
	//if (conn->write_buf.size() > 0) {
	//	conn->state = STATE_WRITE;	
	//}
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
			struct pollfd pfd = {conn->fd, POLLERR, 0};
			if (conn->state == STATE_READ) { pfd.events |= POLLIN; }		
			if (conn->state == STATE_WRITE) { pfd.events |= POLLOUT; }
			poll_args.push_back(pfd);
		}
		int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1); 
		if (rv < 0) { die("poll"); }

		// if the server socket has an event, handle it, .revents is a bitmask of events that have occurred
		if (poll_args[0].revents) {
			if (Conn* conn = handle_accept(fd)) {
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
				handle_read(conn);																			// 	handle_read will read data from the connection and store it in the read buffer
				printf("Exit handle_read, state: %i\n", conn->state);
			}
			if (ready & POLLOUT) {
				printf("Ready to write on %i\n", conn->fd);
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

