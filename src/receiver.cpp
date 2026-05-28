#include <iostream>
#include <portaudio.h>
#include <opus/opus.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include "../include/rtp.h"
#include <thread>
#include <mutex>
#include <queue>
#include <vector>

#define SAMPLE_RATE 48000
#define CHANNELS 1
#define FRAME_SIZE 960
#define PORT 5000

//int16_t audioBuffer[FRAME_SIZE];

OpusDecoder *decoder;

struct AudioFrame {
    std::vector<int16_t> samples;
};

std::queue<AudioFrame> jitterBuffer;

std::mutex bufferMutex;

static int audioCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData
) {

    int16_t *out = (int16_t*)outputBuffer;

    std::lock_guard<std::mutex> lock(bufferMutex);

    if (!jitterBuffer.empty()) {

        AudioFrame frame =
            jitterBuffer.front();

        jitterBuffer.pop();

        for (int i = 0; i < FRAME_SIZE; i++) {
            *out++ = frame.samples[i];
        }

    } else {

        for (int i = 0; i < FRAME_SIZE; i++) {
            *out++ = 0;
        }
    }

    return paContinue;
}

int main() {

    int err;

    decoder = opus_decoder_create(
        SAMPLE_RATE,
        CHANNELS,
        &err
    );

    if (err != OPUS_OK) {
        std::cerr << "Error creando decodificador opus\n";
        return 1;
    }

    int sockfd;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr));

    std::thread networkThread([&]() {

    while (true) {

        char packet[4096];

        int bytesReceived = recvfrom(
            sockfd,
            packet,
            sizeof(packet),
            0,
            nullptr,
            nullptr
        );

        if (bytesReceived <= (int)sizeof(RTPHeader))
            continue;

        RTPHeader header{};

        memcpy(
            &header,
            packet,
            sizeof(RTPHeader)
        );

        unsigned char *opusData =
            (unsigned char*)
            (packet + sizeof(RTPHeader));

        int opusSize =
            bytesReceived - sizeof(RTPHeader);

        std::vector<int16_t> pcm(
            FRAME_SIZE
        );

        int decodedSamples =
            opus_decode(
                decoder,
                opusData,
                opusSize,
                pcm.data(),
                FRAME_SIZE,
                0
            );

        if (decodedSamples > 0) {

            std::lock_guard<std::mutex>
                lock(bufferMutex);

            jitterBuffer.push({
                pcm
            });

            if (jitterBuffer.size() > 50) {
                jitterBuffer.pop();
            }
        }
    }
});

networkThread.detach();

    Pa_Initialize();

    PaStream *stream;

    Pa_OpenDefaultStream(
        &stream,
        0,
        1,
        paInt16,
        SAMPLE_RATE,
        FRAME_SIZE,
        audioCallback,
        nullptr
    );

    Pa_StartStream(stream);

    std::cout << "receptor con opus iniciado en puerto: " << PORT << "\n";
    std::cout << "Presiona enter para salir\n";
    std::cin.get();

    opus_decoder_destroy(decoder);

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    close(sockfd);

    return 0;
}