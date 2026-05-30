#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>

#define CONTROL_PORT 5001

int main(int argc, char* argv[]) {

    std::string my_name = (argc > 1) ? argv[1] : "Buscador_Anonimo";

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

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in broadcastAddr{};

    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port =
        htons(CONTROL_PORT);

    broadcastAddr.sin_addr.s_addr =
        inet_addr("255.255.255.255");

    std::string message =
        "DISCOVER:" + my_name;

    sendto(
        sockfd,
        message.c_str(),
        message.length(),
        0,
        (sockaddr*)&broadcastAddr,
        sizeof(broadcastAddr)
    );

    std::cout
        << "Buscando usuarios (Broadcast enviado por " << my_name << "). Espere 2 segundos...\n\n";
    std::cout << "=== LISTA DE USUARIOS ACTIVOS ===\n";

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

        if (bytesReceived < 0) {
            break;
        }

        if (bytesReceived > 0) {

            buffer[bytesReceived] = '\0';
            std::string resp(buffer);

            char ip[INET_ADDRSTRLEN];

            inet_ntop(
                AF_INET,
                &senderAddr.sin_addr,
                ip,
                sizeof(ip)
            );

            if (resp.rfind("DISCOVER_RESPONSE:", 0) == 0) {
                std::string user_name = resp.substr(18);
                std::cout << "[+] Nombre: " << user_name << " | IP: " << ip << "\n";
            }
        }
    }

    std::cout << "=================================\n";
    std::cout << "Busqueda finalizada.\n";

    close(sockfd);

    return 0;
}