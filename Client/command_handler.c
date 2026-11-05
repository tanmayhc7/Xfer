#include "command_handler.h"

//Global variables declared extern in command_handler.h
int server_socket;
sock_s server;
int addr_length;
int server_bytes;

/*
   It takes pointer to the payload and the length for checksum as input arguments.5
   This Function calculates checksum and returns an "int" checksum. It uses mod 100 checksum algorithm
   */
int checksum(char * buffer, int length)
{
	int i;
	int checksum = 0;
	for(i=0;i<length;i++)
	{
		checksum = (checksum + *buffer)%100;
	}
	return checksum;
}

/*
   This function encrypts and decrypts entire files.
   This function uses XOR for encryption. It performs character by character encryption and uses a character key.
   */
void encrypt_data(char* filename)
{
	char c;
	char key = 'j';
	FILE * fp;
	fp = fopen(filename,"rw");

	c = getc(fp);
	while(c!=EOF)
	{
		fputc(c^key,fp);
		c = fgetc(fp);
	}

	fclose(fp);
}

/*
   Parse input command and call corresponding function
   */
void decode_command(char *cmd){

	if(!cmd) return;

	char* command;
	char* filename;
	command = strtok(cmd, "\n ");
	filename = strtok(NULL, "\n ");

	if(!command) return;

	printf("Command: %s\n",command);
	printf("Filename: %s\n",filename);

	if(strcmp(command,"put")==0)
	{
		send_file(filename);
	} else if(strcmp(command,"get")==0){
		receive_file(filename);
	} else if(strcmp(command,"delete")==0){
		delete_file(filename);
	} else if(strcmp(command,"list")==0){
		list_file();
	} else if(strcmp(command,"exit")==0){
		printf("Exiting\n");
		close(server_socket);			//close sock before exit
		exit(EXIT_SUCCESS);
	} else{
		char message [50];
		bzero(message,sizeof(message));
		printf("Invalid Command\n");
		recvfrom(server_socket,message,sizeof(message),0,  (struct sockaddr *)&server, (socklen_t *)&addr_length);
		printf("%s",message);
	}
}

void send_file(char * filename){
	char ack__get[35]; //server to client
	char ack__send[200]; //client to server

	char message[50];//to send messages from client to server 
	bzero(message,sizeof(message));

	//check if file exists on client side
	if(access(filename,F_OK) == -1)
	{
		sprintf(message,"File does not exist");
		sendto(server_socket, message, strlen(message), 0, (struct sockaddr *)&server, addr_length);
		printf("%s\n",message);
		return;
	} else{
		sprintf(message,"File exists");
		sendto(server_socket, message, strlen(message), 0, (struct sockaddr *)&server, addr_length);
	}

	//check if file exists on server side
	server_bytes = recvfrom(server_socket, ack__get, sizeof(ack__get), 0, (struct sockaddr *)&server, (socklen_t *)&addr_length);
	if(strcmp(ack__get,"File already exists on Server")==0){
		printf("%s\n",ack__get);
		char c;
		printf("File will be overwritten. Do you want to continue? (Y/N) : ");
		c=fgetc(stdin);
		if(c=='N' || c=='n'){
			bzero(message,sizeof(message));
			sprintf(message,"Overwrite Cancelled");
			sendto(server_socket, message, strlen(message), 0, (struct sockaddr *)&server, addr_length);
			printf("%s\n",message);
			return;	
		} else{
			bzero(message,sizeof(message));
			sprintf(message,"Overwriting file on Server");
			sendto(server_socket, message, strlen(message), 0, (struct sockaddr *)&server, addr_length);
			printf("%s\n",message);
		}       	
	} else{
		bzero(message,sizeof(message));
		sprintf(message,"Sending file");
		sendto(server_socket, message, strlen(message), 0, (struct sockaddr *)&server, addr_length);
		printf("%s\n",message);
	}



	//encode file data 
	encrypt_data(filename);

	printf("Sending File %s...\n",filename);

	FILE *fp;
	fp=fopen(filename,"r");

	fseek(fp,0,SEEK_END);
	long int file_size=ftell(fp);

	printf("File size = %ld\n",file_size);

	sprintf(ack__send,"%ld",file_size);
	//send file size to receiver
	server_bytes = sendto(server_socket, ack__send, strlen(ack__send), 0, (struct sockaddr *)&server, addr_length);       

	if(file_size<=0){
		printf("Error with the file..filesize less than 0.. :( \n");
		fclose(fp);
		return;
	}

	//set time out for recvfrom() so as to resend for packet/ack drop
	struct timeval tv;
	bzero(&tv,sizeof(tv));
	tv.tv_sec = 1;  /* 1 Secs Timeout */
	tv.tv_usec = 0; 
	setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(tv));

	rewind(fp); //put fp back to the start

	//Code to send the file using packets
	packet_s p;
	//check how many times the loop has to run to send the whole file
	int num_loops_to_run=file_size/sizeof(p.payload); //will store the floor() of the divisor
	int rem=file_size%sizeof(p.payload); //check if there is still some bytes remaining 
	bool last_packet=0;
	if(rem>0) last_packet=1;
	bool flag=0;

	int seek_pos=0;

	p.seq_no=1;
	p.checksum=0;
	//for large files that cannot be sent in one go
	if(file_size > sizeof(p.payload)){
		for(int i=0;i<num_loops_to_run;i++){
			printf("Sending Packet Number %d\n",p.seq_no);
			//send until ack received
			while(!flag)
			{
				//set fp to the start of the block to be copied
				fseek(fp,seek_pos,SEEK_SET);

				//copy 1000B block to p.payload
				fread(p.payload,sizeof(p.payload),1,fp);
				p.checksum = checksum(p.payload,sizeof(p.payload));

				server_bytes = sendto(server_socket, (void *)&p, sizeof(p), 0, (struct sockaddr *)&server, addr_length);

				bzero(ack__get,sizeof(ack__get));

				server_bytes = recvfrom(server_socket, ack__get, sizeof(ack__get), 0, (struct sockaddr *)&server, (socklen_t *)&addr_length);

				if(server_bytes>0 && strcmp(ack__get,"received")==0)
				{
					//if ack received set flag to 1 and read next chunk     
					flag = 1;
					p.seq_no++;
					seek_pos += sizeof(p.payload);
				} else{
					printf("\t\t........TIMEOUT.......\n");
				}
			}
			flag = 0;	
		}			
	}
	//For last chunk of data remaining or the whole data.
	//Skip when the file is an exact multiple of the payload size: the loop above already sent everything.
	bool send_tail = (rem > 0) || (file_size <= (long)sizeof(p.payload));
	if(send_tail) printf("Sending Packet Number %d\n",p.seq_no);
	fseek(fp,seek_pos,SEEK_SET);
	if(last_packet){
		fread(p.payload,1,rem,fp);
	} else{
		fread(p.payload,1,file_size,fp);	
	}
	p.checksum = checksum(p.payload,(file_size-seek_pos));
	//resend till ack is received
	while(send_tail && !flag)
	{
		server_bytes = sendto(server_socket, (void *)&p, sizeof(p), 0, (struct sockaddr *)&server, addr_length);
		bzero(ack__get,sizeof(ack__get));
		server_bytes = recvfrom(server_socket, ack__get, sizeof(ack__get), 0, (struct sockaddr *)&server, (socklen_t *)&addr_length);
		if(server_bytes > 0 && strcmp(ack__get,"received") == 0){
			flag = 1;
		} else {
			printf("\t\t........TIMEOUT.......\n");
		} 
	}
	flag = 0;

	fclose(fp);

	//Reset timeout for restof the program
	tv.tv_sec = 0;  /* Timeout */
	tv.tv_usec = 0;  
	setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

	//decode file data 
	encrypt_data(filename);	

	printf("\n\n\t\t\t FILE SENT...");
}

void receive_file(char * filename){

	char message[50];
	bzero(message,sizeof(message));

	server_bytes = recvfrom(server_socket, message, sizeof(message), 0, (struct sockaddr *)&server, (socklen_t *)&addr_length);

	//verify if file exists
	if(strcmp(message,"Requested file not found")==0)
	{
		printf("%s\n",message);
		return;
	}

	if(access(filename,F_OK)!=-1){
		printf("File already exists on your system..\n");
		printf("Do you want to continue receiving the file? (Y/N) ");
		char c;
		c=fgetc(stdin);

		if(c=='Y' || c=='y'){
			remove(filename);
			server_bytes=sendto(server_socket, "continue sending", strlen("continue sending"), 0, (struct sockaddr *)&server, addr_length);
		} else{
			server_bytes=sendto(server_socket, "stop", strlen("stop"), 0, (struct sockaddr *)&server, addr_length);
			return;
		}

	} else{
		server_bytes=sendto(server_socket, "continue sending", strlen("continue sending"), 0, (struct sockaddr *)&server, addr_length);	
	}

	printf("%s\n",message);	

	//received filename
	printf("Receiving File %s...\n",filename);

	//receive file size from server
	char buffer[100];
	bzero(buffer,sizeof(buffer));
	server_bytes = recvfrom(server_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&server, (socklen_t *)&addr_length);

	int file_size = atoi(buffer);

	if(!file_size){
		printf("File Size Error... :(\n");
		return;
	}

	printf("Size of the file to be received: %d\n",file_size);

	//Open a new file and start copying data
	FILE *fp;
	fp=fopen(filename,"w");//open file in write mode to create a new file

	packet_s p;	


	long int expected_seq_no=1;

	bool flag=0;

	long int num_bytes_received=0;

	while(num_bytes_received < file_size)
	{

		//recieve packet untill packet drop
		while(!flag)
		{
			server_bytes=recvfrom(server_socket,(void*)&p, sizeof(p), 0, (struct sockaddr *)&server, (socklen_t *)&addr_length);

			printf("Receiving Packet Number %d\n",p.seq_no);

			//if packet recieved is as expected then write to file and send ack
			if(server_bytes>0 && p.seq_no == expected_seq_no)
			{
				flag = 1;
				expected_seq_no++;
				//if the packet recieved is thelast for a big file or first for a small file
				if(p.seq_no == (int)(file_size/sizeof(p.payload)) + 1)
				{
					if(p.checksum != checksum(p.payload,(file_size - ((int)file_size/sizeof(p.payload)) * sizeof(p.payload))))
					{
						printf("\t\t\tChecksum Does not match\n");
					}

					fwrite(p.payload,1,(file_size - ((int)file_size/sizeof(p.payload)) * sizeof(p.payload)),fp);
				}
				//if the packet is initial packet of a big file
				else
				{
					if(p.checksum != checksum(p.payload,sizeof(p.payload)))
					{
						printf("\t\t\tChecksum Does not match\n");
					}

					fwrite(p.payload,1,sizeof(p.payload),fp);
				}
				num_bytes_received = num_bytes_received + server_bytes - 8;

				server_bytes=sendto(server_socket, "received", 8, 0, (struct sockaddr *)&server, addr_length);
			}
			//ifpacket recieved is 1 less than expected meaning ack got dropped. hence dont rewrite packet, just respond with ack
			else if(server_bytes>0 && (p.seq_no == expected_seq_no - 1))
			{
				flag = 1;
				server_bytes=sendto(server_socket, "received", 8, 0, (struct sockaddr *)&server, addr_length);	
			}
		}
		flag = 0;
	}	

	fclose(fp);
	//decode the file sent by the server
	encrypt_data(filename);

	printf("\n\n\t\t\t FILE RECEIVED...\n");	
}


/*
   This function generates a list.txt file containing ls -al output at the server and this file is displayed to the client.
   */
void list_file(void){

	receive_file("list.txt");

	char c;

	FILE * fp;
	fp = fopen("list.txt","r");

	//display the list.txt file	
	c = fgetc(fp);
	while(c!=EOF)
	{
		printf("%c",c);
		c = fgetc(fp);
	}

	fclose(fp);

	remove("list.txt");
}

void delete_file(char * filename){
	char message[50];
	bzero(message,sizeof(message));

	server_bytes = recvfrom(server_socket, message, sizeof(message), 0, (struct sockaddr *)&server, (socklen_t *)&addr_length);

	printf("%s\n",message);	
}

