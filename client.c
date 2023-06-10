#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

typedef struct
{
	int seq_ack;
	int len;
	int cksum;
} Header;

typedef struct
{
	Header header;
	char data[1024];
} Packet;

struct sockaddr_in server_address;

int calculate_checksum(Packet packet)
{
	packet.header.cksum = 0;
	int checksum = 0;
	char *ptr = (char *)&packet;
	char *end = ptr + sizeof(Header) + packet.header.len;
	while (ptr < end)
	{
		checksum ^= *ptr++;
	}
	return checksum;
}

void printPacket(Packet packet)
{
	printf("[PACKET] -- { header: { seq_ack: %d, len: %d, cksum: %d }, data: \"",
		   packet.header.seq_ack,
		   packet.header.len,
		   packet.header.cksum);
	fwrite(packet.data, (size_t)packet.header.len, 1, stdout);
	printf("\" }\n");
}

void clientSend(int sockfd, struct sockaddr *address, socklen_t addrlen, Packet packet, unsigned retries)
{
	while (1)
	{
		if (retries >= 3)
		{
			break;
		}

		packet.header.cksum = calculate_checksum(packet);

		if (rand() % 2 == 0)
			printf("[SIMULATE] -- dropping packet\n");
		else if (rand() % 5 == 0) {
			printf("[SIMULATE] -- wrong checksum\n");
			packet.header.cksum = 0;
			sendto(sockfd, &packet, sizeof(packet), 0, address, addrlen);
		} else if (rand() % 5 == 0) {
			printf("[SIMULATE] -- wrong sequence number\n");
            packet.header.seq_ack = (packet.header.seq_ack + 1) % 2;
            sendto(sockfd, &packet, sizeof(packet), 0, address, addrlen);
		}
		else
		{
			printf("[LOG] -- sending packet\n");
			sendto(sockfd, &packet, sizeof(packet), 0, address, addrlen);
		}

		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		int rv;

		fd_set readfds;
		fcntl(sockfd, F_SETFL, O_NONBLOCK);

		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);

		rv = select(sockfd + 1, &readfds, NULL, NULL, &tv);

		if (rv == 0)
		{
			printf("[ERROR] -- timeout\n");
			retries++;
		}
		else
		{
			Packet recvpacket;
			recvfrom(sockfd, &recvpacket, sizeof(recvpacket), 0, address, &addrlen);
			printPacket(recvpacket);

			printf("[LOG] -- received ack %d, checksum %d - \n", recvpacket.header.seq_ack, recvpacket.header.cksum);

			int e_cksum = calculate_checksum(recvpacket);

			if (e_cksum != recvpacket.header.cksum)
			{
				printf("[ERROR] -- bad checksum, expected checksum: %d, \t received checksum: %d\n", e_cksum, recvpacket.header.cksum);
			}
			else if (packet.header.seq_ack != recvpacket.header.seq_ack)
			{
				printf("[ERROR] -- bad seqnum, expected sequence number: %d, received sequence number: %d\n", packet.header.seq_ack, recvpacket.header.seq_ack);
			}
			else
			{
				printf("[LOG] -- good ack\n");
				break;
			}
		}
	}
}

int open_socket(char *host_address, int port_number)
{
	printf("[LOG] -- opening socket\n");
	int sockfd;

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("cannot create socket\n");
		return 0;
	}

	struct hostent *host;
	host = (struct hostent *)gethostbyname(host_address);

	memset(&server_address, 0, sizeof(server_address));

	server_address.sin_port = htons(port_number);
	server_address.sin_family = AF_INET;
	server_address.sin_addr = *((struct in_addr *)host->h_addr);

	return sockfd;
}

void validate_args(int argc, char *argv[])
{
	if (argc != 4)
	{
		printf("Usage: %s <ip> <port> <srcfile>\n", argv[0]);
		exit(0);
	}
}

int open_file(char *filename)
{
	printf("[LOG] -- opening file\n");
	int file_fd = open(filename, O_RDWR);
	if (file_fd < 0)
	{
		perror("failed to open file\n");
		exit(1);
	}
	return file_fd;
}

void send_packets(int socket_fd, int file_fd)
{
	printf("[LOG] -- sending packets\n");
	int seq = 0;
	socklen_t addr_len = sizeof(server_address);
	Packet packet;
	int bytes;
	while ((bytes = read(file_fd, packet.data, sizeof(packet.data))) > 0)
	{
		packet.header.seq_ack = seq;
		packet.header.len = bytes;
		packet.header.cksum = calculate_checksum(packet);
		clientSend(socket_fd, (struct sockaddr *)&server_address, addr_len, packet, 0);
		seq = (seq + 1) % 2;
	}

	Packet final;
	final.header.seq_ack = seq;
	final.header.len = 0;
	final.header.cksum = calculate_checksum(final);
	clientSend(socket_fd, (struct sockaddr *)&server_address, addr_len, final, 0);

	close(file_fd);
	close(socket_fd);
}

int main(int argc, char *argv[])
{
	validate_args(argc, argv);
	int socket_fd = open_socket(argv[1], atoi(argv[2]));
	int file_fd = open_file(argv[3]);
	send_packets(socket_fd, file_fd);
	return 0;
}