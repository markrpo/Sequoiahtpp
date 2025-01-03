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

#define msg printf
const size_t k_max_msg = 4096;

static void die(const char* msg) {
	perror(msg);
	exit(1);
}

static void do_something(int fd) { 										// static means that this function is only visible in this file
	char rbuff[1024];
	ssize_t bytes_read = read(fd, rbuff, sizeof(rbuff) - 1); 				// read from the file descriptor
	
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

static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error, or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t query(int fd, const char *text) {
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg) {
        return -1;
    }
    // send request
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);  // assume little endian
    memcpy(&wbuf[4], text, len);
    if (int32_t err = write_all(fd, wbuf, 4 + len)) {
        return err;
    }
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }
    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }
    // reply body
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }
    // do something
    rbuf[4 + len] = '\0';
    printf("server says: %s\n", &rbuf[4]);
    return 0;
}


int main () {
	
	// Create a file descriptor
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }
	
	// Create a sockaddr_in struct to store the address and port of the server
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1
	
	// Connect to the server using the file descriptor and the sockaddr_in struct
	// bind can be used also to bind the file descriptor to a specific address and port, if not binded the OS will choose a random port and address
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) {
        die("connect");
    }

	// getsockname can be used to get the address and port of the file descriptor
    struct sockaddr_in addrc;
	socklen_t lenc = sizeof(addrc);
	getsockname(fd, (struct sockaddr *)&addrc, &lenc);
	char addrc_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &addrc.sin_addr, addrc_str, sizeof(addrc_str));

	// getpeername can be used to get the address and port of the server
	struct sockaddr_in addrs;
	socklen_t lens = sizeof(addrs);
	getpeername(fd, (struct sockaddr *)&addrs, &lens);
	char addrs_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &addrs.sin_addr, addrs_str, sizeof(addrs_str));

	printf("Connected from %s:%i\n", addrc_str, ntohs(addrc.sin_port));
	printf("Connected to %s:%i\n", addrs_str, ntohs(addrs.sin_port));

	int32_t err = query(fd, "hello1");
    if (err) {
       	goto L_DONE;
    }
    err = query(fd, "hello2");
    if (err) {
        goto L_DONE;
    }
    err = query(fd, "hello3");
    if (err) {
        goto L_DONE;
    }

	L_DONE:
    	close(fd);
    return 0;
}
