#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define RECV_TIMEOUT 10
#define RECV_RETRIES 10

char next_char = 0;

 enum opcode {
	RRQ = 1,
	WRQ,
	DATA,
	ACK,
	ERROR
};
enum mode {
	NETASCII = 1,
	OCTET = 2,
};
typedef union {
 	uint16_t opcode;
 	struct {
		uint16_t opcode; 
		uint8_t filename_and_mode[514];
	} request;
 	struct {
		uint16_t opcode; 
		uint16_t block_number;
		uint8_t data[512];
	} data;
 	struct {
		uint16_t opcode; 
		uint16_t block_number;
	} ack;
 	struct {
		uint16_t opcode; 
		uint16_t error_code;
		uint8_t error_string[512];
	} error;
 } tftp_message;


 ssize_t tftp_send_data(int s, uint16_t block_number, uint8_t *data,
	ssize_t dlen, struct sockaddr_in *sock, socklen_t slen)
{
	tftp_message m;
	ssize_t c;
 	m.opcode = htons(DATA);
	m.data.block_number = htons(block_number);
	memcpy(m.data.data, data, dlen);
	c = sendto(s, &m, 4 + dlen, 0,	(struct sockaddr *) sock, slen);
 	return c;
}

 ssize_t tftp_send_error(int s, uint16_t error_code, struct sockaddr_in *sock, socklen_t slen)
{
	tftp_message m;
	ssize_t c;
	int dlen;
 	m.opcode = htons(ERROR);
	char errormsg1[] = "File not found";
	m.error.error_code = 1;
	/*
	switch(error_code){
		case 1: printf("%s\n", errormsg1);
				dlen = strlen(errormsg1);
				memcpy(m.error.error_string, errormsg1, dlen);
		default:
			return 0;
	}
	*/
	printf("%s\n", errormsg1);
	dlen = strlen(errormsg1);
	memcpy(m.error.error_string, errormsg1, dlen);
	c = sendto(s, &m, 4 + dlen, 0,	(struct sockaddr *) sock, slen);
 	return c;
}

 ssize_t tftp_send_ack(int s, uint16_t block_number,
	struct sockaddr_in *sock, socklen_t slen)
{
	tftp_message m;
	ssize_t c;
 	m.opcode = htons(ACK);
	m.ack.block_number = htons(block_number);
	c = sendto(s, &m, sizeof(m.ack), 0, (struct sockaddr *) sock, slen);
 	return c;
}
 
 ssize_t tftp_recv_message(int s, tftp_message *m, struct sockaddr_in *sock, socklen_t *slen)
{
	ssize_t c;
	c = recvfrom(s, m, sizeof(*m), 0, (struct sockaddr *) sock, slen);  
 	return c;
}
 
 int main(int argc, char *argv[])
{
	int s;
	int port = 0;
	struct protoent *pp;
	struct servent *ss;
	struct sockaddr_in server_sock;
 	if (argc < 2) {
		printf("usage: [ip address] [port]\n");
		exit(1);
	}
 	char* server_address = argv[1]; 
	sscanf(argv[2], "%hu", &port);
	port = htons(port);
	ss = getservbyname("tftp", "udp");
	pp = getprotobyname("udp");
	s = socket(AF_INET, SOCK_DGRAM, pp->p_proto);
	server_sock.sin_family = AF_INET;
	server_sock.sin_addr.s_addr = inet_addr(server_address);
	server_sock.sin_port = port ? port : ss->s_port;
 	if(bind(s, (struct sockaddr *) &server_sock, sizeof(server_sock))<0)
		perror("socket binding failed, please try with sudo\n");
 	printf("tftp server is listening\n");
 	while (1) {
		struct sockaddr_in client_sock;
		socklen_t slen = sizeof(client_sock);
		int len;
 		tftp_message message;
		uint16_t opcode;
		len = tftp_recv_message(s, &message, &client_sock, &slen);
		opcode = ntohs(message.opcode);
		if (opcode == RRQ || opcode == WRQ) {
			if (fork() == 0) {
				int s;
				struct protoent *pp;
				struct timeval tv;
				char *filename, *mode_s, *end;
				FILE *fd;
				int mode;
				uint16_t opcode;
				pp = getprotobyname("udp");
				s = socket(AF_INET, SOCK_DGRAM, pp->p_proto);
				tv.tv_sec = RECV_TIMEOUT;
				tv.tv_usec = 0;
				setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
				filename = message.request.filename_and_mode;
				end = &filename[len - 2 - 1];
				mode_s = strchr(filename, '\0') + 1;
				opcode = ntohs(message.opcode);
				fd = fopen(filename, opcode == RRQ ? "r" : "w");
				if(fd == NULL){
				    tftp_send_error(s, 1, &client_sock, slen);
					exit(1);
				}
				mode = strcasecmp(mode_s, "netascii") ? NETASCII :strcasecmp(mode_s, "octet") ? OCTET :0;
				printf("request received\n");
				printf("The mode is %d\n", mode);

				if (opcode == RRQ &&(mode == 1 || mode == 2)) {
					tftp_message m;
					uint8_t data[512];
					int dlen, c;
					uint16_t block_number = 0;
					int countdown;
					int to_close = 0;
					if(mode == 1){
						while (!to_close) {
							dlen = fread(data, 1, sizeof(data), fd);
							block_number++;
							if (dlen < 512) { 
								to_close = 1;
							}
							for (countdown = RECV_RETRIES; countdown; countdown--) {
								c = tftp_send_data(s, block_number, data, dlen, &client_sock, slen);
								c = tftp_recv_message(s, &m, &client_sock, &slen);
								if(countdown != 10){
									printf("The remaining retries nunber is %d.\n", countdown);
								}
								if (c >= 4) {
									break;
								}
							}
							if (!countdown) {
								printf("transfer timed out\n");
								exit(1);
							}
						}
					}else{
						while(!to_close){
							block_number++;
							int count_r;
							char* ptr;
							char charc;
							ptr = &(data[0]);
							for(count_r = 0; count_r < 512; count_r++){
								if(next_char >= 0){
									*ptr++ = next_char;
									next_char = -1;
									continue;
								}

								charc = getc(fd);

								if(charc == EOF){
									if(ferror(fd)){
									//	err_dump("read err from getc on local file");
									}
									to_close = 1;
									break;
								}else if(charc == '\n'){
									charc = '\r';
									next_char = '\n';
								}else if(charc == '\r'){
									next_char = '\0';
								}else{
									next_char = -1;
								}
								*ptr++ = charc;
							}

							for(countdown = RECV_RETRIES; countdown; countdown--){
								c = tftp_send_data(s, block_number, data, count_r, &client_sock, slen);
								c = tftp_recv_message(s, &m, &client_sock, &slen);
								if(countdown != 10){
									printf("The remaining retries nunber is %d.\n", countdown);
								}
								if (c >= 4) {
									break;
								}
							}
							if (!countdown) {
								/*
								char* err_word = "The requested filename is not exit.\n";
								printf("%s", err_word);
								int dlen = strlen(err_word);
								memcpy(data, error_word, dlen);
				    			tftp_send_error(s, 0, data, dlen, &client_sock, slen);
								exit(1);
								*/
								printf("transfer timed out\n");
								exit(1);
							}
						}
					}
				} if (opcode == WRQ) {
					tftp_message m;
					ssize_t c;
					uint16_t block_number = 0;
					int countdown;
					int to_close = 0;
					c = tftp_send_ack(s, block_number, &client_sock, slen);
					while (!to_close) {
						for (countdown = RECV_RETRIES; countdown; countdown--) {
							c = tftp_recv_message(s, &m, &client_sock, &slen);
							if (c >= 4) {
								break;
							}
							c = tftp_send_ack(s, block_number, &client_sock, slen);
						}
						if (!countdown) {
							printf("transfer timed out\n");
							exit(1);
						}
						block_number++;
						if (c < sizeof(m.data)) {
							to_close = 1;
						}
						c = fwrite(m.data.data, 1, c - 4, fd);
						c = tftp_send_ack(s, block_number, &client_sock, slen);
					}
				}
				printf("transfer completed\n");
				fclose(fd);
			}
		}
	}
 	close(s);
 	return 0;
}
