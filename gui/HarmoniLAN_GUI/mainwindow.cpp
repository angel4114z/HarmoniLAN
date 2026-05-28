#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QNetworkDatagram>

#include <portaudio.h>
#include <opus/opus.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../../include/rtp.h"
#include <queue>
#include <vector>
#include <mutex>
#include <thread>

// Puerto de recepción de audio (según tu código)
#define RECEIVER_PORT 5000 
#define FRAME_SIZE 960

// Variables globales del RECEPTOR
OpusDecoder *opusDecoder;
int receiverSockfd;

struct AudioFrame {
    std::vector<int16_t> samples;
};
std::queue<AudioFrame> jitterBuffer;
std::mutex bufferMutex;

// CALLBACK DEL RECEPTOR (Tu código exacto de consola con Jitter Buffer)
static int receiverAudioCallback(
    const void *inputBuffer, void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags, void *userData
) {
    int16_t *out = (int16_t*)outputBuffer;
    std::lock_guard<std::mutex> lock(bufferMutex);

    if (!jitterBuffer.empty()) {
        AudioFrame frame = jitterBuffer.front();
        jitterBuffer.pop();
        for (int i = 0; i < FRAME_SIZE; i++) {
            *out++ = frame.samples[i];
        }
    } else {
        // Silencio si el buffer está vacío
        for (int i = 0; i < FRAME_SIZE; i++) {
            *out++ = 0;
        }
    }
    return paContinue;
}



#define SAMPLE_RATE 48000
#define CHANNELS 1
#define FRAME_SIZE 960
#define AUDIO_PORT 5000 // Puerto para el audio (RTP)

// Variables globales para el streaming
int audioSockfd;
sockaddr_in audioServerAddr;
OpusEncoder *opusEncoder;
PaStream *audioStream;

uint16_t sequenceNumber = 0;
uint32_t timestamp = 0;
uint32_t ssrc = 12345;

// El puente de control: un booleano global accesible por el callback
std::atomic<bool> enviarAudio(false);

// TU CALLBACK ORIGINAL CON EL FILTRO PTT
static int audioCallback(
    const void *inputBuffer, void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags, void *userData
) {
    if (inputBuffer == nullptr) return paContinue;

    // !!! EL TRUCO DEL PTT !!!
    // Si el botón NO está presionado, ignoramos el micrófono y no enviamos nada por red
    if (!enviarAudio.load()) {
        return paContinue;
    }

    const int16_t *pcm = (const int16_t*)inputBuffer;
    unsigned char opusData[4000];

    int encodedBytes = opus_encode(opusEncoder, pcm, FRAME_SIZE, opusData, sizeof(opusData));

    if (encodedBytes > 0) {
        char packet[4096];
        RTPHeader header{}; // Estructura rtp.h que ya tienes
            
        header.versionPayload = 0x80;
        header.payloadType = 111;
        header.sequenceNumber = htons(sequenceNumber++);
        header.timestamp = htonl(timestamp);
        header.ssrc = htonl(ssrc);
            
        memcpy(packet, &header, sizeof(RTPHeader));
        memcpy(packet + sizeof(RTPHeader), opusData, encodedBytes);
        
        sendto(audioSockfd, packet, sizeof(RTPHeader) + encodedBytes, 0,
               (sockaddr*)&audioServerAddr, sizeof(audioServerAddr));
        
        timestamp += FRAME_SIZE;
    }

    return paContinue;
}



MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 1. Inicializar Socket de Control y Señales de Qt (Lo que ya tenías)
    udpControlSocket = new QUdpSocket(this);
    connect(udpControlSocket, &QUdpSocket::readyRead, this, &MainWindow::procesarRespuestaUDP);

    // =================================================================
    // NÚCLEO DEL RECEPTOR (Código de tu receiver.cpp integrado en Qt)
    // =================================================================
    int err;
    opusDecoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    
    receiverSockfd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in recAddr{};
    recAddr.sin_family = AF_INET;
    recAddr.sin_port = htons(RECEIVER_PORT); // Escucha en el puerto 5000
    recAddr.sin_addr.s_addr = INADDR_ANY;
    ::bind(receiverSockfd, (sockaddr*)&recAddr, sizeof(recAddr));

    // Lanzamos tu hilo nativo de red (Jitter Buffer)
    std::thread receiverNetworkThread([this]() {
        while (true) {
            char packet[4096];
            int bytesReceived = recvfrom(receiverSockfd, packet, sizeof(packet), 0, nullptr, nullptr);
            if (bytesReceived <= (int)sizeof(RTPHeader)) continue;

            RTPHeader header{};
            memcpy(&header, packet, sizeof(RTPHeader));

            unsigned char *opusData = (unsigned char*)(packet + sizeof(RTPHeader));
            int opusSize = bytesReceived - sizeof(RTPHeader);

            std::vector<int16_t> pcm(FRAME_SIZE);
            int decodedSamples = opus_decode(opusDecoder, opusData, opusSize, pcm.data(), FRAME_SIZE, 0);

            if (decodedSamples > 0) {
                std::lock_guard<std::mutex> lock(bufferMutex);
                jitterBuffer.push({pcm});
                if (jitterBuffer.size() > 50) {
                    jitterBuffer.pop();
                }
            }
        }
    });
    receiverNetworkThread.detach();

    // Arrancar PortAudio para la REPRODUCCIÓN (Output)
    Pa_Initialize();
    PaStream *receiverStream;
    // Notar que pasamos '1' en outputChannels y '0' en inputChannels
    Pa_OpenDefaultStream(&receiverStream, 0, 1, paInt16, SAMPLE_RATE, FRAME_SIZE, receiverAudioCallback, nullptr);
    Pa_StartStream(receiverStream);

    ui->txtLogs->append("Sistema Receptor escuchando activamente en puerto 5000...");
    
    // =================================================================
    // HILO DE RESPUESTA AL BROADCAST (Tu discovery.cpp integrado)
    // =================================================================
    std::thread discoveryThread([]() {
        int discSock = socket(AF_INET, SOCK_DGRAM, 0);
        int broadcastEnable = 1;
        setsockopt(discSock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(5001); // Puerto de control
        addr.sin_addr.s_addr = INADDR_ANY;
        
        if (::bind(discSock, (sockaddr*)&addr, sizeof(addr)) >= 0) {
            while (true) {
                char buffer[1024];
                sockaddr_in senderAddr{};
                socklen_t senderLen = sizeof(senderAddr);
                int bytes = recvfrom(discSock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&senderAddr, &senderLen);
                if (bytes > 0) {
                    buffer[bytes] = '\0';
                    if (strcmp(buffer, "DISCOVER") == 0) {
                        const char* response = "DISCOVER_RESPONSE:HarmoniLAN";
                        sendto(discSock, response, strlen(response), 0, (sockaddr*)&senderAddr, senderLen);
                    }
                }
            }
        }
    });
    discoveryThread.detach();
}

MainWindow::~MainWindow()
{
    // Detener PortAudio y liberar Opus limpiamente al salir
    if (audioStream) {
        Pa_StopStream(audioStream);
        Pa_CloseStream(audioStream);
        Pa_Terminate();
    }
    if (opusEncoder) {
        opus_encoder_destroy(opusEncoder);
    }
    ::close(audioSockfd);
    if (opusDecoder) {
        opus_decoder_destroy(opusDecoder);
    }
    ::close(receiverSockfd);
    
    delete ui;
}

// 1. LO QUE VA EXACTAMENTE EN TU BOTÓN BUSCAR:
void MainWindow::on_btnBuscar_clicked()
{
    // Escribimos en tu Text Edit (consola visual)
    ui->txtLogs->append("Enviando Broadcast de búsqueda...");

    QByteArray mensaje = "DISCOVER";
    
    // Enviamos el broadcast al puerto 5001 tal como lo tenías en tu código de consola
    udpControlSocket->writeDatagram(mensaje, QHostAddress::Broadcast, 5001);
}

// 2. LA FUNCIÓN QUE RECIBE LA RESPUESTA DE LA RED (Pégala justo abajo):
void MainWindow::procesarRespuestaUDP()
{
    while (udpControlSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpControlSocket->receiveDatagram();
        QString respuesta = QString::fromUtf8(datagram.data());

        // Verificamos si la respuesta es la de tu servidor HarmoniLAN
        if (respuesta.contains("DISCOVER_RESPONSE:HarmoniLAN")) {
            QString ipDetectada = datagram.senderAddress().toString();

            if (ipDetectada.startsWith("::ffff:")) ipDetectada = ipDetectada.mid(7);

                ui->txtLogs->append("¡Equipo detectado! IP: " + ipDetectada);

                // === INICIALIZACIÓN DEL AUDIO EX-SENDER.CPP ===
                int err;
                opusEncoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err);

                audioSockfd = socket(AF_INET, SOCK_DGRAM, 0);
                audioServerAddr.sin_family = AF_INET;
                audioServerAddr.sin_port = htons(AUDIO_PORT); // Puerto 5000
                inet_pton(AF_INET, ipDetectada.toStdString().c_str(), &audioServerAddr.sin_addr);

                Pa_Initialize();
                Pa_OpenDefaultStream(&audioStream, 1, 0, paInt16, SAMPLE_RATE, FRAME_SIZE, audioCallback, nullptr);
                Pa_StartStream(audioStream);

                ui->txtLogs->append("Canal de audio RTP listo. Mantén presionado PTT para hablar.");
                ui->btnPTT->setEnabled(true); // Ya podemos hablar
            }
    }
}

// CUANDO MANTIENES PRESIONADO EL BOTÓN
void MainWindow::on_btnPTT_pressed()
{
    enviarAudio.store(true); // Abre la compuerta en el callback de PortAudio
    ui->btnPTT->setText("¡TRANSMITIENDO EN VIVO!");
    ui->btnPTT->setStyleSheet("background-color: #ff4d4d; color: white; font-weight: bold;");
}

// CUANDO SUELTAS EL BOTÓN
void MainWindow::on_btnPTT_released()
{
    enviarAudio.store(false); // Cierra la compuerta, el audio deja de enviarse
    ui->btnPTT->setText("Presionar para Hablar (PTT)");
    ui->btnPTT->setStyleSheet(""); // Vuelve al color normal
}

void MainWindow::on_chkHablaContinua_toggled(bool checked)
{
    if (checked) {
        // Activamos la transmisión permanente
        enviarAudio.store(true);

        // Desactivamos el botón de PTT para que el usuario no se confunda
        ui->btnPTT->setEnabled(false);
        ui->btnPTT->setText("Micrófono Abierto (Habla Continua)");
        ui->btnPTT->setStyleSheet("background-color: #2ecc71; color: white; font-weight: bold;"); // Color verde

        ui->txtLogs->append("Modo habla continua activado. Micrófono abierto.");
    } else {
        // Apagamos la transmisión permanente
        enviarAudio.store(false);

        // Reactivamos el botón PTT a su estado normal
        ui->btnPTT->setEnabled(true);
        ui->btnPTT->setText("Presionar para Hablar (PTT)");
        ui->btnPTT->setStyleSheet(""); // Color normal

        ui->txtLogs->append("Modo habla continua desactivado. Volviendo a PTT.");
    }
}

