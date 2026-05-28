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

    sockaddr_in addr{};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONTROL_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(
        sockfd,
        (sockaddr*)&addr,
        sizeof(addr)
    );

    std::cout
        << "Esperando discovery...\n";

    while (true) {

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

            std::cout
                << "Mensaje: "
                << buffer
                << "\n";

            if (
                strcmp(buffer, "DISCOVER")
                == 0
            ) {

                const char* response =
                    "DISCOVER_RESPONSE:HarmoniLAN";

                sendto(
                    sockfd,
                    response,
                    strlen(response),
                    0,
                    (sockaddr*)&senderAddr,
                    senderLen
                );
            }
        }
    }

    close(sockfd);

    return 0;
}