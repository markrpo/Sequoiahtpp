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
#include <map>
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

const size_t MAX_BUF_SIZE = 32 << 20; 												// 32 MB

struct Conn{
		int fd = -1;
		uint8_t state = STATE_READ;
		size_t read_size = 0;
		uint8_t read_buf[4+MAX_BUF_SIZE];
		size_t write_size = 0;
		uint8_t write_buf[4+MAX_BUF_SIZE];
};

/* Conn struct buffers could be allocated in heap when is constructed using:
struct Conn{
	int fd = -1;
	uint8_t state = STATE_READ;
	size_t read_size = 0;
	uint8_t* read_buf;
	size_t write_size = 0;
	uint8_t* write_buf;
	Conn() {
		read_buf = new uint8_t[4+MAX_BUF_SIZE];
		write_buf = new uint8_t[4+MAX_BUF_SIZE];
	}
	~Conn() {
		delete[] read_buf;
		delete[] write_buf;
	}
}; but sizeof(read_buf) and sizeof(write_buf) will be 8 bytes (size of a pointer) */

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

static std::map<std::string, std::string> g_map;

int32_t real_request(const uint8_t* data, Conn* conn, uint32_t len, uint32_t* rescode, uint32_t* wlen) {
	// first 4 bytes are the number of strings in the request, each string is prefixed with a 4 byte length
	uint32_t n = 0;
	memcpy(&n, data, 4);
	std::vector<std::string> reqs;
	uint32_t offset = 4;
	while (n--) { 																								// 	Iterate over the strings in the request
		uint32_t slen = 0;
		memcpy(&slen, data + offset, 4);																		// 	Copy 4 bytes from the data buffer to slen (length of the string)
		offset += 4;
		std::string s((const char*)data + offset, slen);
		offset += slen;
		reqs.push_back(s);
	}
	// process the request
	if (reqs.size() == 2 && reqs[0] == "get") {
		*rescode = RES_OK;
		const char *msg = "Hello, World!";
		*wlen = strlen(msg);
		return 0;
	} else if (reqs.size() == 3 && reqs[0] == "set") {
		//(void)res;
		(void)wlen;																								// (void) is used to cast a variable to void, so the compiler doesn't complain about unused variables
		g_map[reqs[1]] = reqs[2];
		*rescode = RES_OK;
		return 0;
	} else if (reqs.size() == 2 && reqs[0] == "del") {
		//(void)res;
		(void)wlen;
		g_map.erase(reqs[1]);
		*rescode = RES_OK;
		return 0;
	} else {
		*rescode = RES_ERR;
		const char *msg = "Unknown cmd";
		*wlen = strlen(msg);
		return 0;
	}
	return 0;
}

int32_t do_request(const uint8_t* data, Conn* conn, uint32_t len, uint32_t* rescode, uint32_t* wlen) {
	// This do request only echoes the message back to the client
	*rescode = RES_OK;
	*wlen = len;
	printf("Copied %i bytes\n", (int)len);
	memcpy(&conn->write_buf[conn->write_size], &len, 4);														//  Coppy to the end of the write buffer (that is the write_size variable) the length of the message (len, 4 bytes long)
	conn->write_size += 4;
	memcpy(&conn->write_buf[conn->write_size], data, len);
	conn->write_size += len;
	return 0;
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
	printf("Wrote %i bytes\n", (int)rv);
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
	printf ("Still data to write\n");
	return true;
}

bool parse_request (Conn* conn){
	if (conn->read_size < 4) { return false; }
	uint32_t len = 0;
   	printf("Parsing request\n");	
	memcpy(&len, &conn->read_buf[0], 4);																		// 	Copy 4 bytes from the read buffer to len
	if (len > MAX_BUF_SIZE) { printf("msg too long\n"); conn->state = STATE_CLOSE; return false; }
																												// 	Could also use uint32_t len = *(uint32_t*)conn->read_buf.data(); (uint32_t size is 4 bytes)
	if (conn->read_size < 4 + len) { return false; }															// 	If the buffer is smaller than 4 + len, we don't have a complete message
	const uint8_t* data = &conn->read_buf[4];																	// 	Data points to the start of the message (without the length)

	printf("Received of length: %i\n", (int)len);																// 	Print the length of the message and print part of the message;
	printf("Message: %.*s\n", (int)len < 10 ? (int)len : 10, (const char*)data);								// 	%.*s is a format specifier that takes two arguments, the first is the length of the string and the second is the string
																												// 	if the length is less than 10, print the whole string, otherwise print the first 10 characters
	
	uint32_t rescode = 0;
	uint32_t wlen = 0;
	int32_t err = do_request(data, conn, len, &rescode, &wlen);
	//int32_t err = do_request(data, conn->write_buf.data() + (4+4), len, &rescode, &wlen);	
	if (err) { conn->state = STATE_CLOSE; return false; }

	wlen += 4;
	
	// Remove the message from the read buffer 
	size_t remain = conn->read_size - (4 + len);
	if (remain > 0)	{
		memmove(&conn->read_buf[0], &conn->read_buf[4 + len], remain);
	}
	conn->read_size = remain;

	conn->state = STATE_WRITE;								
	while (handle_write(conn)) { 
	}																											// 	Write until we send all the data (or we get EAGAIN (kernel buffer full))
	printf ("handle_write returned false\n");
	return true;																								// 	Return true if a message was parsed (to continue parsing even if we didnt wrote the message)
}

bool handle_read(Conn* conn) {
	printf("Readin from %i\n", conn->fd);
	uint8_t* buf = new uint8_t[MAX_BUF_SIZE];																	// 	Not optimal, we used new to allocate memory for the buffer because it is too big to be allocated on the stack 
																												// 	(max stack size is 8MB on linux) and heap size is much bigger
	ssize_t rv = read(conn->fd, buf, (conn->read_size - sizeof(buf)));											// 	Read from the connection and store the data in buf

	if (rv < 0 && errno == EAGAIN) {
		printf("returning EAGAIN (read)\n");
		return false;
	}																	
	if (rv < 0) {
		conn->state = STATE_CLOSE;
		return false;
	}
	if (rv == 0) {
		printf("EOF\n");
		conn->state = STATE_CLOSE;
		return false;
	}
	printf("Read %i bytes\n", (int)rv);
	mempcpy(&conn->read_buf[conn->read_size], buf, rv);															// 	Append the data to the read buffer
	conn->read_size += rv;																						// 	Increase the size of the read buffer
	printf("Read size: %i\n", (int)conn->read_size);
	printf("Size of read_buf: %i\n", (int)sizeof(conn->read_buf));
	assert(conn->read_size <= sizeof(conn->read_buf));
	while(parse_request(conn)){ }																				// 	See if we have a complete request (will stay in the loop until we don't have a complete request)
	//if (conn->write_buf.size() > 0) {
	//	conn->state = STATE_WRITE;	
	//}
	delete[] buf;
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

