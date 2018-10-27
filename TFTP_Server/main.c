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

 enum opcode {
	RRQ = 1,
	WRQ,
	DATA,
	ACK,
	ERROR
};
enum mode {
	NETASCII = 1,
	OCTET
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
char *base_directory;

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
		printf("usage: [base directory] [port]\n");
		exit(1);
	}
 	base_directory = argv[1];
	sscanf(argv[2], "%hu", &port);
	port = htons(port);
	ss = getservbyname("tftp", "udp");
	pp = getprotobyname("udp");
	s = socket(AF_INET, SOCK_DGRAM, pp->p_proto);
	server_sock.sin_family = AF_INET;
	server_sock.sin_addr.s_addr = htonl(INADDR_ANY);
	server_sock.sin_port = port ? port : ss->s_port;
 	bind(s, (struct sockaddr *) &server_sock, sizeof(server_sock));
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
				mode = strcasecmp(mode_s, "netascii") ? NETASCII :strcasecmp(mode_s, "octet") ? OCTET :0;
				printf("request received\n");

				if (opcode == RRQ) {
					tftp_message m;
					uint8_t data[512];
					int dlen, c;
					uint16_t block_number = 0;
					int countdown;
					int to_close = 0;
					while (!to_close) {
						dlen = fread(data, 1, sizeof(data), fd);
						block_number++;
						if (dlen < 512) { 
							to_close = 1;
						}
						for (countdown = RECV_RETRIES; countdown; countdown--) {
							c = tftp_send_data(s, block_number, data, dlen, &client_sock, slen);
							c = tftp_recv_message(s, &m, &client_sock, &slen);
							if (c >= 4) {
								break;
							}
						}
						if (!countdown) {
							printf("transfer timed out\n");
							exit(1);
						}
					}
				}
				else if (opcode == WRQ) {
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
