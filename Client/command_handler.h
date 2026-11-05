#pragma once

//Standard Libs
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <memory.h>

//SOCKET related libraries
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

//Preprocessor directives
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

typedef struct sockaddr_in sock_s;

/*
   This is structure of packet to be sent to and from the server and client. It consists of the sequence number and checksum and payload
   */
typedef struct
{
	int seq_no;
	int checksum;
	char payload[1000];
}packet_s;

//Function prototypes
extern void send_file(char *);
extern void receive_file(char *);
extern void list_file(void);
extern void delete_file(char *);
extern int checksum(char *, int);
extern void decode_command(char*);
extern int checksum(char *, int);

//Global variables (defined once in command_handler.c)
extern int server_socket;
extern sock_s server;
extern int addr_length;
extern int server_bytes;
