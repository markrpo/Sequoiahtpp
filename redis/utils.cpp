#include "utils.hpp"

void die(const char* msg) {
	perror(msg);
	exit(1);
}

void set_nonblock(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) { die("fcntl(F_GETFL)"); }
	int rv = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (rv == -1) { die("fcntl(F_SETFL)"); }
}

void do_something(int fd) { 											
	char rbuff[1024];
	ssize_t bytes_read = read(fd, rbuff, sizeof(rbuff) - 1); 				
	
	if (bytes_read < 0){
		perror("read");
		return;
	}

	printf("Read %i bytes: %.*s\n", bytes_read, (int)bytes_read, rbuff); 	
	
	char wbuff[] = "Hello, client!";
	ssize_t bytes_written = write(fd, wbuff, strlen(wbuff)); 				
}

void get_adress(int fd) {
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	int rv = getpeername(fd, (struct sockaddr*)&addr, &addr_len);
	if (rv) { die("getpeername"); }
	char addr_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &addr.sin_addr, addr_str, sizeof(addr_str));
	printf("Client address: %s\n", addr_str);
	printf("Client port: %i\n", ntohs(addr.sin_port));
}


int32_t read_all(int fd, char* buf, size_t count) {
	while ( count > 0) {
		ssize_t rv = read(fd, (char*)buf, count); 		
		if (rv <= 0) { return -1; }
		assert((size_t)rv <= count); 			
		count -= (size_t)rv; 				
		buf += rv; 						
	}
	return 0;

}

int32_t write_all (int fd, char* buf, size_t count) {
	while (count > 0) {
		ssize_t rv = write(fd, buf, count);
		if (rv <= 0) { return -1; }
		assert((size_t)rv <= count);
		count -= (size_t)rv;
		buf += rv;
	}
	return 0;
}

int32_t one_request(int connfd) {
    // 4 bytes header 
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_all(connfd, rbuf, 4);
    if (err) {
        if (errno == 0) {			
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }
    uint32_t len = 0;											
    memcpy(&len, rbuf, 4);  	
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }
    
	err = read_all(connfd, &rbuf[4], len);	
    if (err) {
        msg("read() error");
        return err;
    }
    
	rbuf[4 + len] = '\0';
    printf("client says: %s\n", &rbuf[4]);
    
	const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}

