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

#ifndef UTILS_HPP 
#define UTILS_HPP

#define msg printf 		

void die(const char* msg); 


void set_nonblock(int fd);


void do_something(int fd); 											


void get_adress(int fd);



int32_t read_all(int fd, char* buf, size_t count); 

int32_t write_all (int fd, char* buf, size_t count);

const size_t k_max_msg = 4096;

int32_t one_request(int connfd);

#endif
