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
#include <fcntl.h>
/* 	In this "Protocol" the data length is sent in the first 4 bytes of the message
 	Other options are: Delimiter based protocol (but you need to escape the delimiter in the message)
	Text based protocol (like HTTP that uses \r\n as the delimiter at the end of the message)
	Redis uses delimeters and prefix length (human readable) */


#define msg printf 															// define a macro to print messages

static void die(const char* msg) {
	perror(msg);
	exit(1);
}

static void do_something(int fd) { 											// static means that this function is only visible in this file
	char rbuff[1024];
	ssize_t bytes_read = read(fd, rbuff, sizeof(rbuff) - 1); 				// read from the file descriptor
																			// read returns the number of bytes read
																			// read returns 0 if the connection is closed
	
	if (bytes_read < 0){
		perror("read");
		return;
	}

	printf("Read %i bytes: %.*s\n", bytes_read, (int)bytes_read, rbuff); 	// print the read data
	
	// Write to the file descriptor read and write can be replaced with send and recv
	// send and recv can be used with flags like MSG_DONTWAIT, MSG_WAITALL (usded to read all the data)
	char wbuff[] = "Hello, client!";
	ssize_t bytes_written = write(fd, wbuff, strlen(wbuff)); 				// write to the file descriptor
}

static void get_adress(int fd) {
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	int rv = getpeername(fd, (struct sockaddr*)&addr, &addr_len);
	if (rv) { die("getpeername"); }
	char addr_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &addr.sin_addr, addr_str, sizeof(addr_str));
	printf("Client address: %s\n", addr_str);
	printf("Client port: %i\n", ntohs(addr.sin_port));
}

void set_nonblock(int fd) {											/* 	Set the file descriptor to non-blocking mode
																		fcntl is a function that can be used to change the file descriptor
																		flags. F_GETFL is used to get the flags and F_SETFL is used to set
																		the flags. O_NONBLOCK is a flag that makes the file descriptor non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);                                                                                                                                                                                                    
    if (flags == -1) { die("fcntl(F_GETFL)"); }                                                                                                                                                                                           
    int rv = fcntl(fd, F_SETFL, flags | O_NONBLOCK);                                                                                                                                                                                      
    if (rv == -1) { die("fcntl(F_SETFL)"); }                                                                                                                                                                                              
}

// To read bytes from a tcp socket must be done in a loop:

static int32_t read_all(int fd, char* buf, size_t count) { 			// receives a file descriptor, a buffer and the number of bytes to read
	while ( count > 0) {
		ssize_t rv = read(fd, (char*)buf, count); 					// read from the file descriptor (returns the number of bytes read) and count is the max number of bytes to read
		if (rv <= 0) { return -1; }
		assert((size_t)rv <= count); 										// assert is used to check if the condition is true
		count -= (size_t)rv; 										// decrement the count by the number of bytes read
		buf += rv; 													// increment the buffer pointer by the number of bytes read (char is 1 byte)
	}
	return 0;

}

static int32_t write_all (int fd, char* buf, size_t count) {
	while (count > 0) {
		ssize_t rv = write(fd, buf, count);
		if (rv <= 0) { return -1; }
		assert((size_t)rv <= count);
		count -= (size_t)rv;
		buf += rv;
	}
	return 0;
}

const size_t k_max_msg = 4096;

static int32_t one_request(int connfd) {
    // 4 bytes header 
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_all(connfd, rbuf, 4);					// read the first 4 bytes (in our protocol this is the length of the message)
    if (err) {
        if (errno == 0) {										// if errno is 0 then the connection is closed
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }
    uint32_t len = 0;											
    memcpy(&len, rbuf, 4);  									// mempcy copies 4 bytes from rbuf to len
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }
    // request body
    err = read_all(connfd, &rbuf[4], len);						// read the next len bytes
    if (err) {
        msg("read() error");
        return err;
    }
    // do something
    rbuf[4 + len] = '\0';
    printf("client says: %s\n", &rbuf[4]);
    // reply using the same protocol
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}


int main () {
	
	// Create a socket file descriptor
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); 	// set socket option (SOL_SOCKET is the level
																 	// of the option, SO_REUSEADDR is the option name)

	// Asociate the socket (fd) with an address and port
	struct sockaddr_in addr = {};									// This struct holds the address and port information:
	addr.sin_family = AF_INET;
	addr.sin_port = htons(1234);
	addr.sin_addr.s_addr = ntohl(0); 								// htonl and ntohl convert the byte order of an integer
	int rv = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (rv) { die("bind"); } 										// die is a function that prints an error message and exits
	
	// Listen for incoming connections
	rv = listen(fd, 10); 											// listen for incoming connections
	if (rv) { die("listen"); }

	// Accept incoming connections
	while(true) {
		// Create a new struct to hold the client's address and port
		struct sockaddr_in client_addr = {};
		socklen_t client_addr_len = sizeof(client_addr);

		// Accept the connection and get a new file descriptor for the client
		int client_fd = accept(fd, (struct sockaddr*)&client_addr, &client_addr_len);
		if (client_fd < 0) { continue; }
		//do_something(client_fd);
		get_adress(client_fd);

		while (true){
		int32_t err = one_request(client_fd); 						// The one request function will read 1 request and
																	// write 1 response but: How do we know how many bytes to read?
																	// Ussually a protocol has 2 levls of structures:
																	// 1. A high level structure that split strean bytes into messages
																	// 2. The structure within a message
		printf("\nrequest done\n");
		if (err == -1) { break; }
		}
		close(client_fd);
	}
}
