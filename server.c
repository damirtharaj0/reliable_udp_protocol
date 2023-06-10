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

struct sockaddr_in server_address, client_address;

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

void serverSend(int socket_fd, struct sockaddr *address, socklen_t addrlen, int seqnum)
{
	if (rand() % 5 == 0)
	{
		printf("[SIMULATE] -- dropping ack\n");
		return;
	}
	Packet packet;
	packet.header.seq_ack = seqnum;
	packet.header.len = 0;
	packet.header.cksum = calculate_checksum(packet);

	printf("[LOG] -- sending acknowledgement");
	sendto(socket_fd, &packet, sizeof(packet), 0, address, addrlen);
}

Packet serverReceive(int socket_fd, struct sockaddr *address, socklen_t *addrlen, int seqnum)
{
	Packet packet;
	while (1)
	{
		recvfrom(socket_fd, &packet, sizeof(packet), 0, address, addrlen);

		printPacket(packet);

		int e_cksum = calculate_checksum(packet);

		if (packet.header.cksum != e_cksum)
		{
			printf("[ERROR] -- bad checksum \t expected: %d \t recieved: %d\n", e_cksum, packet.header.cksum);
			serverSend(socket_fd, address, *addrlen, !seqnum);
		}
		else if (packet.header.seq_ack != seqnum)
		{
			printf("[ERROR] -- bad seqnum \t expected: %d \t recieved: %d\n", seqnum, packet.header.seq_ack);
			serverSend(socket_fd, address, *addrlen, !seqnum);
		}
		else
		{
			printf("[LOG] -- good packet\n");
			serverSend(socket_fd, address, *addrlen, seqnum);
			break;
		}
	}

	return packet;
}

void run_server(int socket_fd, int file_fd)
{
	printf("[LOG] -- running server\n");
	int seqnum = 0;
	Packet packet;
	socklen_t addr_len = sizeof(client_address);
	do
	{
		packet = serverReceive(socket_fd, (struct sockaddr *)&client_address, &addr_len, seqnum);
		printf("[LOG] -- writing to file\n");
		write(file_fd, packet.data, packet.header.len);
		seqnum = (seqnum + 1) % 2;
	} while (packet.header.len != 0);

	close(file_fd);
	close(socket_fd);
}

int open_file(char *filename)
{
	int file_fd = open(filename, O_CREAT | O_RDWR, 0666);
	if (file_fd < 0)
	{
		perror("cannot open file\n");
		exit(1);
	}
	return file_fd;
}

int open_socket(int port_number)
{
	printf("[LOG] -- opening socket\n");
	int socket_fd;
	if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("cannot create socket\n");
		return 0;
	}

	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port_number);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(socket_fd, (const struct sockaddr *)&server_address, sizeof(server_address)) < 0)
	{
		perror("cannot bind\n");
		exit(1);
	}

	return socket_fd;
}

void validate_args(int argc, char *argv[])
{
	if (argc != 3)
	{
		printf("Usage: %s <port> <dstfile>\n", argv[0]);
		exit(0);
	}
}

int main(int argc, char *argv[])
{
	validate_args(argc, argv);
	int socket_fd = open_socket(atoi(argv[1]));
	int file_fd = open_file(argv[2]);
	run_server(socket_fd, file_fd);
	return 0;
}