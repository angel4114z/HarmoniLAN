#include <iostream>
#include <portaudio.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#define SAMPLE_RATE 48000
#define FRAMES_PER_BUFFER 256
#define PORT 5000

float audioBuffer[FRAMES_PER_BUFFER];

static int audioCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData
) {
    float *out = (float*)outputBuffer;

    for (unsigned int i = 0; i < framesPerBuffer; i++) {
        *out++ = audioBuffer[i];
    }

    return paContinue;
}

int main() {

    int sockfd;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr));

    Pa_Initialize();

    PaStream *stream;

    Pa_OpenDefaultStream(
        &stream,
        0,
        1,
        paFloat32,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        audioCallback,
        nullptr
    );

    Pa_StartStream(stream);

    std::cout << "receptor iniciado puerto: " << PORT << "\n";

    while (true) {

        recvfrom(
            sockfd,
            audioBuffer,
            sizeof(audioBuffer),
            0,
            nullptr,
            nullptr
        );
    }

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    close(sockfd);

    return 0;
}