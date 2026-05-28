#include <iostream>
#include <portaudio.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#define SAMPLE_RATE 48000
#define FRAMES_PER_BUFFER 256
#define PORT 5000

int sockfd;
sockaddr_in serverAddr;

static int audioCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData
) {

    if (inputBuffer != nullptr) {

        sendto(
            sockfd,
            inputBuffer,
            framesPerBuffer * sizeof(float),
            0,
            (sockaddr*)&serverAddr,
            sizeof(serverAddr)
        );
    }

    return paContinue;
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        std::cout << "Uso: ./sender <IP_DESTINO>\n";
        return 1;
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);

    inet_pton(AF_INET, argv[1], &serverAddr.sin_addr);

    Pa_Initialize();

    PaStream *stream;

    Pa_OpenDefaultStream(
        &stream,
        1,
        0,
        paFloat32,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        audioCallback,
        nullptr
    );

    Pa_StartStream(stream);

    std::cout << "Enviando audio a " << argv[1] << "\n";
    std::cout << "Presiona enter para salir\n";

    std::cin.get();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    close(sockfd);

    return 0;
}