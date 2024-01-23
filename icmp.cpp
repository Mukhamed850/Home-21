#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <csignal>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <netdb.h>
#include <iostream>

#define ICMP_PACKET_SIZE 64
#define MAX_ATTEMPTS 4
#define MAX_TIMEOUT 1
#define SLEEP_TIME 1000000

void HandleSig(int sig) {
    std::cerr << "Ping interrupted." << std::endl;
    exit(1);
}

int EvaluateCheckSum(unsigned short *buffer, int length) {
    unsigned long sum = 0;
    while (length > 1) {
        sum += *buffer++;
        length -= 2;
    }
    if (length > 0)
        sum += *(unsigned char *)buffer;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (unsigned short)(~sum);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "You need to enter: " << argv[0] << " <hostname or IP address>" << std::endl;
        exit(1);
    }
    const char *targetHost = argv[1];
    int socket_file_descriptor = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socket_file_descriptor < 0) {
        perror("socket");
        exit(1);
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;

    int status = getaddrinfo(targetHost, NULL, &hints, &res);
    if (status != 0) {
        std::cerr << "getaddrinfo: " << gai_strerror(status) << std::endl;
        exit(1);
    }

    struct sockaddr_in destinationAddress;
    memset(&destinationAddress, 0, sizeof(destinationAddress));
    destinationAddress.sin_family = AF_INET;
    destinationAddress.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;

    freeaddrinfo(res);

    signal(SIGINT, HandleSig);

    for (int seq = 0; seq < MAX_ATTEMPTS; ++seq) {
        struct icmphdr icmpHeader;
        icmpHeader.type = ICMP_ECHO;
        icmpHeader.code = 0;
        icmpHeader.un.echo.id = getpid() & 0xFFFF;
        icmpHeader.un.echo.sequence = seq;
        icmpHeader.checksum = 0;
        icmpHeader.checksum = EvaluateCheckSum((unsigned short *)&icmpHeader, sizeof(icmpHeader));

        struct timespec sendTime, recvTime;
        clock_gettime(CLOCK_MONOTONIC, &sendTime);

        sendto(socket_file_descriptor, &icmpHeader, sizeof(icmpHeader), 0, (struct sockaddr *)&destinationAddress, sizeof(destinationAddress));

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket_file_descriptor, &readSet);

        struct timeval timeout;
        timeout.tv_sec = MAX_TIMEOUT;
        timeout.tv_usec = 0;

        int ready = select(socket_file_descriptor + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready < 0) {
            perror("select");
        } else if (ready > 0) {
            recvfrom(socket_file_descriptor, &icmpHeader, sizeof(icmpHeader), 0, nullptr, nullptr);
            clock_gettime(CLOCK_MONOTONIC, &recvTime);
            long double rtt = (recvTime.tv_sec - sendTime.tv_sec) + (recvTime.tv_nsec - sendTime.tv_nsec) / 1e9;
            std::cout << "Reply from " << targetHost << ": icmp_seq=" << seq << " time=" << rtt * 1000 << " ms" << std::endl;
        } else {
            std::cout << "Request timeout for icmp_seq " << seq << std::endl;
        }

        usleep(SLEEP_TIME);
    }

    close(socket_file_descriptor);
    return 0;
}
