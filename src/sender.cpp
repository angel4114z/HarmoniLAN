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

int sockfd;
sockaddr_in serverAddr;

OpusEncoder *encoder;

static int audioCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData
) {

    if (inputBuffer == nullptr)
        return paContinue;

    const int16_t *pcm = (const int16_t*)inputBuffer;

    unsigned char opusData[4000];

    int encodedBytes = opus_encode(
        encoder,
        pcm,
        FRAME_SIZE,
        opusData,
        sizeof(opusData)
    );

    if (encodedBytes > 0) {

        sendto(
            sockfd,
            opusData,
            encodedBytes,
            0,
            (sockaddr*)&serverAddr,
            sizeof(serverAddr)
        );
    }

    return paContinue;
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        std::cout << "Uso: ./sender <IP>\n";
        return 1;
    }

    int err;

    encoder = opus_encoder_create(
        SAMPLE_RATE,
        CHANNELS,
        OPUS_APPLICATION_VOIP,
        &err
    );

    if (err != OPUS_OK) {
        std::cerr << "Error creando encoder\n";
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
        paInt16,
        SAMPLE_RATE,
        FRAME_SIZE,
        audioCallback,
        nullptr
    );

    Pa_StartStream(stream);

    std::cout << "Enviando audio (con opus) hacia: " << argv[1] << "\n";
    std::cout << "Presiona enter para salir\n";

    std::cin.get();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);

    opus_encoder_destroy(encoder);

    Pa_Terminate();

    close(sockfd);

    return 0;
}