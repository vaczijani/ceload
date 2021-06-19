#ifdef _WIN32
	#include <windows.h>
	#pragma comment(lib,"ws2_32.lib")
	typedef int socklen_t;
#else
	#include <unistd.h>
	#include <arpa/inet.h>
	typedef int SOCKET;
	#define SOCKET_ERROR (-1)
	#define INVALID_SOCKET (-1)
#endif

#include <stdio.h>
#include <vector>
#include <exception>
#include <ctype.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <memory>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#define BOOTME_PORT 980
#define MAXTFTP		512		// RFC 1350
#define TFTPHEAD	4
#define MAXBUFFER	(TFTPHEAD + MAXTFTP)

// TFTP codes
#define TFTP_WRQ	2     // Write request (WRQ)
#define TFTP_DATA	3
#define TFTP_ACK	4


std::string format(const char* format, ...) {
	va_list args;
	va_start(args, format);
	int size = vsnprintf(NULL, 0, format, args);
	std::unique_ptr<char> result(new char[size]);
	vsnprintf(result.get(), size, format, args);
	va_end(args);
	return result.get();
}


class UdpServer {
public:
	typedef std::vector<char> Packet;
	UdpServer(int port) {
		socket_ = socket(AF_INET, SOCK_DGRAM, 0);
		if (socket_ == INVALID_SOCKET) throw std::runtime_error("cannot open UDP socket");
		struct sockaddr_in srv_addr;
		memset(&srv_addr, 0, sizeof(srv_addr));
		srv_addr.sin_family = AF_INET;
		srv_addr.sin_addr.s_addr = INADDR_ANY;
		srv_addr.sin_port = htons(port);
		if ( bind(socket_, (struct sockaddr*)&srv_addr, sizeof(srv_addr))) throw std::runtime_error(format("cannot bind UDP socket to port %d", port));
	}
	~UdpServer() {
#ifdef _WIN32
		closesocket(socket_);
#else
		close(socket_);
#endif
	}
	Packet receive() {
		socklen_t len_addr = sizeof(peer_);
		char buffer[MAXBUFFER];
		int res = recvfrom(socket_, buffer, sizeof(buffer), 0, (sockaddr*)&peer_, &len_addr);
		if ((res == 0) || (res == SOCKET_ERROR)) throw std::runtime_error("UDP receive failed");
		return Packet(buffer, buffer + res);
	}
	void send(const Packet& data) {
		int res = sendto(socket_, data.data(), data.size(), 0, (const sockaddr*)&peer_, sizeof(peer_));
		if ((res == 0) || (res == SOCKET_ERROR)) throw std::runtime_error("UDP send failed");
	}
private:
	SOCKET socket_;
	struct sockaddr_in peer_;
};


void expectTftpAck(UdpServer& udp) {
	UdpServer::Packet data = udp.receive();
	if (data.size() < 4 || data[1] != TFTP_ACK) throw std::runtime_error("No ACK");
}

void tftpSend(UdpServer& udp, const UdpServer::Packet& pckt) {
	udp.send(pckt);
	expectTftpAck(udp);
}

int nb0_load(int argc, char* argv[]) {
	UdpServer udp(BOOTME_PORT);
	char buf[MAXBUFFER];
	uint16_t* block = (uint16_t*)(&buf[2]);
	uint16_t blockn = 0;
	printf("User action needed: Interrupt EBOOT and start Download Image now.\n");
	printf("Waiting for BOOTME...\n");
	UdpServer::Packet data = udp.receive();
	printf("Received packet:");
	std::for_each(data.begin(), data.end(), [](char c){
		printf("%c", (isprint((int)c&0xff)) ? c : '.');
	});
	printf("\n");
	if (strncmp(data.data(), "EDBG", 4)) throw std::runtime_error("No BOOTME packet");
	printf("Sending TFTP Header\n");
	memset(buf, 0, 17);
	buf[1] = TFTP_WRQ;
	*block = htons(blockn++);
	strcpy(&buf[2], "boot.bin");
	strcpy(&buf[11], "octet");
	tftpSend(udp, UdpServer::Packet(buf, buf+17));
	printf("Sending Manifest\n");
	memset(buf, 0, 287);
	buf[1] = TFTP_DATA;
	*block = htons(blockn++);
	strcpy(&buf[4], "N000FF");
	buf[10] = 0x0a;
	buf[11] = 0x0a;
	buf[12] = 0x02;
	buf[15] = 0x01;
	buf[26] = 0x03;
	strcpy(&buf[27], "nk.nb0");
	tftpSend(udp, UdpServer::Packet(buf, buf+287));
	blockn = 0;
	printf("Sending TFTP Header\n");
	memset(buf, 0, 17);
	buf[1] = TFTP_WRQ;
	*block = htons(blockn++);
	strcpy(&buf[2], "boot.bin");
	strcpy(&buf[11], "octet");
	tftpSend(udp, UdpServer::Packet(buf, buf+17));
	std::string filename((argc < 2) ? "nk.nb0" : argv[1]);
	std::ifstream file(filename.c_str(), std::ifstream::binary);
	if ( ! file.is_open()) throw std::runtime_error(format("Cannot open file %s", filename.c_str()));
	printf("Sending image...\n");
	for(;;) {
		if (blockn % 1024 == 0) {
			printf(".");
			fflush(stdout);
		}
		file.read(&buf[4], MAXTFTP);
		int length = file.gcount();
		if (length == 0) break;
		buf[0] = 0;
		buf[1] = TFTP_DATA;
		*block = htons(blockn++);
		tftpSend(udp, UdpServer::Packet(buf, buf+MAXTFTP+4));
	}
	printf("\n");
	printf("End Of Transmission\n");
	printf("Send BOOTME ending\n");
	memset(buf,0,28);
	strcpy(&buf[0], "EDBG");
	buf[4] = -1; // 0xFF;
	buf[7] = 2;
	buf[9] = 1;	// 4=jump?
	udp.send(UdpServer::Packet(buf, buf+28));
	return 0;
}


int main(int argc, char* argv[]) {
#ifdef _WIN32
	WSADATA	wsa_data;
	if ( WSAStartup(MAKEWORD(2,2),&wsa_data) ) throw std::runtime_error("WSA startup error");
#endif
	nb0_load(argc, argv);
#ifdef _WIN32
	WSACleanup();
#endif
}
