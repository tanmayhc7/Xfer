#include "command_handler.h"

//Global variables declared extern in command_handler.h
int client_socket;
sock_s client;
int addr_length;
int client_bytes;

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
   This function is used to decode the entered command and call respective functions. 
   The user input is broken into command and filename here
   If an invalid command is entered, this function prints the invalid command
   */

void decode_command(char *cmd){

	if(!cmd) return;
	char * command;
	char * filename;


	command = strtok(cmd, "\n "); 	//command extracted
	filename = strtok(NULL, "\n ");		//filename extracted

	if(!command) return;

	printf("Command: %s\n",command);
	printf("Filename: %s\n",filename);


	if(strcmp(command,"put")==0)
	{
		receive_file(filename); //client says "puts" means server receives file from client
	}
	else if(strcmp(command,"get")==0)
	{
		send_file(filename); //client says "get" means server sends file to client
	}
	else if(strcmp(command,"delete")==0)
	{
		delete_file(filename);
	}
	else if(strcmp(command,"list")==0)
	{
		list_file();
	}
	else if(strcmp(command,"exit")==0)
	{
		printf("Exiting...\n");
		close(client_socket);			//close sock before exit
		exit(EXIT_SUCCESS);
	}	
	else					//echo invalid command
	{
		char buf[50];
		sprintf(buf,"Invalid Command\n");
		client_bytes= sendto(client_socket, buf, strlen(buf), 0 , (struct sockaddr*)&client, addr_length);
		printf("%s",buf);
	}


}

void send_file(char * filename){

	char ack__send[50];//server to client
	char ack__get[200];//client to server

	char message[50];
	bzero(message,sizeof(message));

	//check if file is present on server
	if(access(filename,F_OK) == -1){
		sprintf(message,"Requested file not found");
		client_bytes= sendto(client_socket, message, strlen(message), 0 , (struct sockaddr*)&client, addr_length);
		return;	
	}

	sprintf(message,"File exists...\n");
	client_bytes= sendto(client_socket, message, strlen(message), 0 , (struct sockaddr*)&client, addr_length);

	bzero(message,sizeof(message));
	client_bytes = recvfrom(client_socket, message, sizeof(message), 0 , (struct sockaddr*)&client, (socklen_t *)&addr_length);

	if(strcmp(message,"stop")==0)
	{
		printf("File present on client\n");
		return;
	}

	encrypt_data(filename);

	//create file pointer and start sending data
	FILE* fp;
	fp=fopen(filename,"r");

	packet_s p;

	//get file size
	fseek(fp,0,SEEK_END);
	long int file_size=ftell(fp);

	printf("File size = %ld\n",file_size);

	sprintf(ack__send,"%ld",file_size);
	//send file size to receiver
	client_bytes= sendto(client_socket, ack__send, strlen(ack__send), 0 , (struct sockaddr*)&client, addr_length);

	if(file_size<=0){
		printf("Error with the file..filesize less than 0.. :( \n");
		fclose(fp);
		return;
	}
	//set time out for recvfrom() so as to resend for packet/ack drop
	struct timeval tv;
	tv.tv_sec = 1;  /* 1 Secs Timeout */
	tv.tv_usec = 0;  // Not init'ing this can cause strange errors
	setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

	rewind(fp);


	//check how many times the loop has to run to send the whole file
	int num_loops_to_run=file_size/sizeof(p.payload); //will store the floor() of the divisor
	int rem=file_size%sizeof(p.payload); //check if there is still some bytes remaining 
	bool last_packet=0;
	if(rem>0) last_packet=1;
	bool flag=0;

	int seek_pos=0;

	p.seq_no=1;
	p.checksum=0;

	if(file_size > sizeof(p.payload)){

		for(int i=0;i<num_loops_to_run;i++){
			printf("Sending Packet Number %d\n",p.seq_no);
			while(!flag){

				//set fp to the start of the block to be copied
				fseek(fp,seek_pos,SEEK_SET);

				//copy 1000B block to p.payload
				fread(p.payload,1,sizeof(p.payload),fp);

				p.checksum = checksum(p.payload,sizeof(p.payload));

				client_bytes= sendto(client_socket, (void*)&p, sizeof(p), 0 , (struct sockaddr*)&client, addr_length);

				bzero(ack__get,sizeof(ack__get));

				client_bytes = recvfrom(client_socket, ack__get, sizeof(ack__get), 0 , (struct sockaddr*)&client, (socklen_t *)&addr_length);

				if(client_bytes>0 && strcmp(ack__get,"received")==0)
				{
					//if ack received set flag to 1 and read next chunk     
					flag = 1;
					p.seq_no++;
					seek_pos+= sizeof(p.payload);
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
		fread(p.payload,rem,1,fp);
	} else{
		fread(p.payload,file_size,1,fp);	
	}
	p.checksum = checksum(p.payload,(file_size-seek_pos));
	//resend till ack is received
	while(send_tail && !flag)
	{
		client_bytes = sendto(client_socket, (void*)&p, sizeof(p), 0 , (struct sockaddr*)&client, addr_length);

		bzero(ack__get,sizeof(ack__get));
		client_bytes = recvfrom(client_socket, ack__get, sizeof(ack__get), 0 , (struct sockaddr*)&client, (socklen_t *)&addr_length);
		if(client_bytes > 0 && strcmp(ack__get,"received") == 0){
			flag = 1;
		} else {
			printf("\t\t........TIMEOUT.......\n");
		}
	}
	flag = 0;
	fclose(fp);

	//Reset timeout for restof the program
	tv.tv_sec = 0;  /* Reset Timeout */
	tv.tv_usec = 0;  
	setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

	//decode file data 
	encrypt_data(filename);	

	printf("\n\n\t\t\t FILE SENT...");
}

void receive_file(char * filename){
	int expected_seq_no = 1;

	char buffer[1000];
	bzero(buffer,sizeof(buffer));

	packet_s p;	

	bool flag = 0;

	char message[50];
	bzero(message,sizeof(message));

	//verify if file exists on client side
	client_bytes = recvfrom(client_socket, message, sizeof(message), 0 , (struct sockaddr*)&client, (socklen_t *)&addr_length);

	if(strcmp(message,"File does not exist")==0)
	{	
		printf("%s\n",message);
		return;
	}

	//Send message if file already exists on server
	if(access(filename,F_OK) != -1){
		bzero(message,sizeof(message));
		sprintf(message,"File already exists on Server");
		client_bytes= sendto(client_socket, message, strlen(message), 0 , (struct sockaddr*)&client, addr_length);
		bzero(message,sizeof(message));
		client_bytes = recvfrom(client_socket, message, sizeof(message), 0 , (struct sockaddr*)&client, (socklen_t *)&addr_length);

		if(strcmp(message,"Overwrite Cancelled")==0){
			printf("%s\n",message);
			return;
		}	

	} else{
		bzero(message,sizeof(message));
		sprintf(message,"Ready to copy file on Server");
		client_bytes= sendto(client_socket, message, strlen(message), 0 , (struct sockaddr*)&client, addr_length);
		bzero(message,sizeof(message));
		client_bytes = recvfrom(client_socket, message, sizeof(message), 0 , (struct sockaddr*)&client, (socklen_t *)&addr_length);
		printf("Client sent: %s\n",message);
	}


	long int filesize;
	long int bytes_written = 0;

	//received filename and file size
	printf("Recieving File %s\n",filename);

	client_bytes = recvfrom(client_socket, buffer, sizeof(buffer), 0 , (struct sockaddr*)&client, (socklen_t *)&addr_length);

	filesize = atoi(buffer);

	if(filesize == 0)
	{
		printf("File Size Error\n");
		return;
	}

	printf("Size of file %ld\n",filesize);

	bzero(buffer,sizeof(buffer));

	FILE * fp;
	fp = fopen(filename,"w");

	//bytes written to file are less than total file size
	while(bytes_written < filesize)
	{
		printf("Receiving Packet Number %d\n",expected_seq_no);

		//receive packet untill packet drop
		while(!flag)
		{
			client_bytes = recvfrom(client_socket, (void *)&p, sizeof(p), 0 , (struct sockaddr*)&client, (socklen_t *)&addr_length);

			//if packet received is as expected then write to file and send ack
			if(client_bytes>0 && p.seq_no >= expected_seq_no)
			{	
				flag = 1;
				expected_seq_no++;	
				//if the packet received is thelast for a big file or first for a small file
				if(p.seq_no == (int)(filesize/sizeof(p.payload)) + 1)
				{
					if(p.checksum != checksum(p.payload,(filesize - ((int)filesize/sizeof(p.payload)) * sizeof(p.payload))))
					{
						printf("\t\t\tChecksum Does not match..\n"); //continue;
					}
					fwrite(p.payload,1,(filesize - ((int)filesize/sizeof(p.payload)) * sizeof(p.payload)),fp);
				} else{
					//if the packet is initial packet of a big file
					if(p.checksum != checksum(p.payload,sizeof(p.payload)))
					{
						printf("\t\t\tChecksum Does not match\n"); //continue;
					}
					fwrite(p.payload,1,sizeof(p.payload),fp);
				}
				bytes_written= bytes_written + client_bytes - 8;// since packet_s has 2 ints along with payload

				client_bytes= sendto(client_socket, "received", 8, 0 , (struct sockaddr*)&client, addr_length);
			} else if(client_bytes>0 && (p.seq_no == expected_seq_no -1)){
				// ifpacket received is 1 less than expected meaning ack got dropped. hence dont rewrite packet, just respond with ack
				printf("\t\t\t\t\t\tDROPPED\n");
				flag = 1;
				client_bytes= sendto(client_socket, "received", 8 , 0 , (struct sockaddr*)&client, addr_length);
			}
		}
		flag = 0;
	}
	fclose(fp);

	encrypt_data(filename);

	printf("\n\n\t\t\tFILE RECEIVED..");	
}

/*
   This function is called list command is entered. It generates a list.txt file containing ls -al output and this file is sent back to the client.
   */
void list_file(void){
	system("ls -al > list.txt");
	send_file("list.txt");
	remove("list.txt");
}

//To delete the file from the server
void delete_file(char * filename){
	printf("Deleting %s...\n",filename);

	char message[50];
	bzero(message,sizeof(message));

	int check_removed=remove(filename);

	if(check_removed >=0){
		sprintf(message,"%s has been removed from the server..\n",filename);
	} else{
		sprintf(message,"cannot remove %s from the server..\n",filename);
	}

	client_bytes= sendto(client_socket, message, strlen(message), 0 , (struct sockaddr*)&client, addr_length);

	printf("%s",message);
}
