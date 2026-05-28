#include <iostream>
#include <portaudio.h>
#include <opus/opus.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>

#define SAMPLE_RATE 48000
#define CHANNELS 1
#define FRAME_SIZE 960
#define PORT 5000

int16_t audioBuffer[FRAME_SIZE];

OpusDecoder *decoder;

static int audioCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData
) {

    int16_t *out = (int16_t*)outputBuffer;

    for (int i = 0; i < FRAME_SIZE; i++) {
        *out++ = audioBuffer[i];
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

    while (true) {

        unsigned char opusData[4000];

        int bytesReceived = recvfrom(
            sockfd,
            opusData,
            sizeof(opusData),
            0,
            nullptr,
            nullptr
        );

        if (bytesReceived > 0) {

            int samplesDecoded = opus_decode(
                decoder,
                opusData,
                bytesReceived,
                audioBuffer,
                FRAME_SIZE,
                0
            );

            if (samplesDecoded < 0) {
                std::cerr << "Error al decodificar Opus: " << opus_strerror(samplesDecoded) << std::endl;
            }
        }
    }

    opus_decoder_destroy(decoder);

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    close(sockfd);

    return 0;
}