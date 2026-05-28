#include <iostream>
#include <portaudio.h>

#define SAMPLE_RATE 48000
#define FRAMES_PER_BUFFER 256

static int audioCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData
) {
    const float *in = (const float*)inputBuffer;
    float *out = (float*)outputBuffer;

    if (inputBuffer == nullptr) {
        for (unsigned int i = 0; i < framesPerBuffer; i++) {
            *out++ = 0;
        }
    } else {
        for (unsigned int i = 0; i < framesPerBuffer; i++) {
            *out++ = *in++;
        }
    }

    return paContinue;
}

int main() {
    PaError err;

    err = Pa_Initialize();

    if (err != paNoError) {
        std::cerr << "Error iniciando PortAudio\n";
        return 1;
    }

    PaStream *stream;

    err = Pa_OpenDefaultStream(
        &stream,
        1,                  // input channels
        1,                  // output channels
        paFloat32,          // sample format
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        audioCallback,
        nullptr
    );

    if (err != paNoError) {
        std::cerr << "Error abriendo stream\n";
        Pa_Terminate();
        return 1;
    }

    err = Pa_StartStream(stream);

    if (err != paNoError) {
        std::cerr << "Error iniciando stream\n";
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }

    std::cout << "HarmoniLAN audio loopback iniciado.\n";
    std::cout << "Habla al micrófono...\n";
    std::cout << "Presiona ENTER para salir.\n";

    std::cin.get();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    return 0;
}