#include <iostream>

#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#define CONTROL_PORT 5001

int main() {

    int sockfd =
        socket(AF_INET, SOCK_DGRAM, 0);

    int broadcastEnable = 1;

    setsockopt(
        sockfd,
        SOL_SOCKET,
        SO_BROADCAST,
        &broadcastEnable,
        sizeof(broadcastEnable)
    );

    sockaddr_in broadcastAddr{};

    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port =
        htons(CONTROL_PORT);

    broadcastAddr.sin_addr.s_addr =
        inet_addr("255.255.255.255");

    const char* message =
        "DISCOVER";

    sendto(
        sockfd,
        message,
        strlen(message),
        0,
        (sockaddr*)&broadcastAddr,
        sizeof(broadcastAddr)
    );

    std::cout
        << "Broadcast enviado\n";

    char buffer[1024];

    sockaddr_in senderAddr{};
    socklen_t senderLen =
        sizeof(senderAddr);

    int bytesReceived =
        recvfrom(
            sockfd,
            buffer,
            sizeof(buffer) - 1,
            0,
            (sockaddr*)&senderAddr,
            &senderLen
        );

    if (bytesReceived > 0) {

        buffer[bytesReceived] = '\0';

        char ip[INET_ADDRSTRLEN];

        inet_ntop(
            AF_INET,
            &senderAddr.sin_addr,
            ip,
            sizeof(ip)
        );

        std::cout
            << "Respuesta desde "
            << ip
            << "\n";

        std::cout
            << buffer
            << "\n";
    }

    close(sockfd);

    return 0;
}