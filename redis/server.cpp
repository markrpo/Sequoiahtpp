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
#include "utils.hpp"

int main () {
	
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); 	
	struct sockaddr_in addr = {};

	addr.sin_family = AF_INET;
	addr.sin_port = htons(1234);
	addr.sin_addr.s_addr = ntohl(0); 
	int rv = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (rv) { die("bind"); } 			
	
	rv = listen(fd, 10); 			
	if (rv) { die("listen"); }

	while(true) {

		struct sockaddr_in client_addr = {};
		socklen_t client_addr_len = sizeof(client_addr);

		int client_fd = accept(fd, (struct sockaddr*)&client_addr, &client_addr_len);
		if (client_fd < 0) { continue; }
		
		get_adress(client_fd);

		while (true){
		int32_t err = one_request(client_fd); 
		
		printf("\nrequest done\n");
		if (err == -1) { break; }
		}
		close(client_fd);
	}
}
