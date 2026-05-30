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

#define RECEIVER_PORT 5000 
#define FRAME_SIZE 960

OpusDecoder *opusDecoder;
int receiverSockfd;

struct AudioFrame {
    std::vector<int16_t> samples;
};
std::queue<AudioFrame> jitterBuffer;
std::mutex bufferMutex;

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
        for (int i = 0; i < FRAME_SIZE; i++) {
            *out++ = 0;
        }
    }
    return paContinue;
}



#define SAMPLE_RATE 48000
#define CHANNELS 1
#define FRAME_SIZE 960
#define AUDIO_PORT 5000

// Variables para el sender
int audioSockfd;
sockaddr_in audioServerAddr;
OpusEncoder *opusEncoder;
PaStream *audioStream;

uint16_t sequenceNumber = 0;
uint32_t timestamp = 0;
uint32_t ssrc = 12345;

std::atomic<bool> enviarAudio(false);

std::string miNombreGlobal = "YoGUI";
std::mutex nombreMutex;

static int audioCallback(
    const void *inputBuffer, void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags, void *userData
) {
    if (inputBuffer == nullptr) return paContinue;

    if (!enviarAudio.load()) {
        return paContinue;
    }

    const int16_t *pcm = (const int16_t*)inputBuffer;
    unsigned char opusData[4000];

    int encodedBytes = opus_encode(opusEncoder, pcm, FRAME_SIZE, opusData, sizeof(opusData));

    if (encodedBytes > 0) {
        char packet[4096];
        RTPHeader header{};
            
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
    
    buscandoEquipos = false;
    discoveryTimer = new QTimer(this);
    discoveryTimer->setSingleShot(true);
    connect(discoveryTimer, &QTimer::timeout, this, &MainWindow::finDescubrimiento);
    
    connect(ui->lstUsuarios, &QListWidget::itemClicked, this, &MainWindow::onUsuarioSeleccionado);

    udpControlSocket = new QUdpSocket(this);
    connect(udpControlSocket, &QUdpSocket::readyRead, this, &MainWindow::procesarRespuestaUDP);

    int err;
    opusDecoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    
    receiverSockfd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in recAddr{};
    recAddr.sin_family = AF_INET;
    recAddr.sin_port = htons(RECEIVER_PORT);
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

    Pa_Initialize();
    PaStream *receiverStream;
    Pa_OpenDefaultStream(&receiverStream, 0, 1, paInt16, SAMPLE_RATE, FRAME_SIZE, receiverAudioCallback, nullptr);
    Pa_StartStream(receiverStream);

    ui->txtLogs->append("Receptor iniciado");
    
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
                    std::string msg(buffer);
                    if (msg.rfind("DISCOVER", 0) == 0) {
                        std::string nombreParaEnviar;
                        {
                            std::lock_guard<std::mutex> lock(nombreMutex);
                            nombreParaEnviar = miNombreGlobal;
                        }
                        std::string response = "DISCOVER_RESPONSE:" + nombreParaEnviar;
                        sendto(discSock, response.c_str(), response.length(), 0, (sockaddr*)&senderAddr, senderLen);
                    }
                }
            }
        }
    });
    discoveryThread.detach();
}

MainWindow::~MainWindow()
{
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

void MainWindow::on_btnBuscar_clicked()
{
    ui->lstUsuarios->clear();
    ui->txtLogs->append("Buscando usuarios...");

    buscandoEquipos = true;
    
    QString nombre = ui->txtNombreUsuario->text().trimmed();
    if (nombre.isEmpty()) nombre = "Anonimo_GUI";
    
    QByteArray mensaje = "DISCOVER:" + nombre.toUtf8();

    udpControlSocket->writeDatagram(mensaje, QHostAddress::Broadcast, 5001);

    discoveryTimer->start(2000);
}

void MainWindow::finDescubrimiento()
{
    buscandoEquipos = false;
    ui->txtLogs->append("Búsqueda finalizada. Selecciona un usuario en la lista.");
}

void MainWindow::procesarRespuestaUDP()
{
    while (udpControlSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpControlSocket->receiveDatagram();
        
        if (!buscandoEquipos) continue;

        QString respuesta = QString::fromUtf8(datagram.data());

        if (respuesta.startsWith("DISCOVER_RESPONSE:")) {
            QString ipDetectada = datagram.senderAddress().toString();
            if (ipDetectada.startsWith("::ffff:")) ipDetectada = ipDetectada.mid(7);

            QString nombreUsuario = respuesta.mid(18);

            QString textoElemento = nombreUsuario + " (" + ipDetectada + ")";
            QListWidgetItem *item = new QListWidgetItem(textoElemento, ui->lstUsuarios);
            item->setData(Qt::UserRole, ipDetectada); // Guardar silenciosamente la IP pura
            
            ui->txtLogs->append("Encontrado: " + textoElemento);
        }
    }
}

void MainWindow::onUsuarioSeleccionado(QListWidgetItem *item)
{
    QString ipDetectada = item->data(Qt::UserRole).toString();
    ui->txtLogs->append("Conectando con: " + item->text());

    if (audioStream) {
        Pa_StopStream(audioStream);
        Pa_CloseStream(audioStream);
        audioStream = nullptr;
    }
    if (opusEncoder) {
        opus_encoder_destroy(opusEncoder);
        opusEncoder = nullptr;
    }

    int err;
    opusEncoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err);

    audioSockfd = socket(AF_INET, SOCK_DGRAM, 0);
    audioServerAddr.sin_family = AF_INET;
    audioServerAddr.sin_port = htons(AUDIO_PORT); // Puerto 5000
    inet_pton(AF_INET, ipDetectada.toStdString().c_str(), &audioServerAddr.sin_addr);

    Pa_OpenDefaultStream(&audioStream, 1, 0, paInt16, SAMPLE_RATE, FRAME_SIZE, audioCallback, nullptr);
    Pa_StartStream(audioStream);

    ui->txtLogs->append("Canal de audio RTP listo para " + ipDetectada + ". Puedes hablar.");
    ui->btnPTT->setEnabled(true);
}

// CUANDO MANTIENES PRESIONADO EL BOTÓN
void MainWindow::on_btnPTT_pressed()
{
    enviarAudio.store(true);
    ui->btnPTT->setText("Transmitiendo");
    ui->btnPTT->setStyleSheet("background-color: #ff4d4d; color: white; font-weight: bold;");
}

void MainWindow::on_btnPTT_released()
{
    enviarAudio.store(false); // Cierra la compuerta, el audio deja de enviarse
    ui->btnPTT->setText("PTT");
    ui->btnPTT->setStyleSheet(""); // Vuelve al color normal
}

void MainWindow::on_chkHablaContinua_toggled(bool checked)
{
    if (checked) {
        // Activar la transmisión permanente
        enviarAudio.store(true);

        // Desactivar el botón de PTT
        ui->btnPTT->setEnabled(false);
        ui->btnPTT->setText("Transmitiendo");
        ui->btnPTT->setStyleSheet("background-color: #2ecc71; color: white; font-weight: bold;"); //

        ui->txtLogs->append("Modo habla continua activado. Micrófono abierto.");
    } else {
        // Apagar la transmisión permanente
        enviarAudio.store(false);

        // Reactivar el botón PTT a su estado normal
        ui->btnPTT->setEnabled(true);
        ui->btnPTT->setText("PTT");
        ui->btnPTT->setStyleSheet("");

        ui->txtLogs->append("Modo habla continua desactivado. Volviendo a PTT.");
    }
}

void MainWindow::on_txtNombreUsuario_textChanged(const QString &arg1)
{
    QString nombre = arg1.trimmed();
    if (nombre.isEmpty()) nombre = "Anonimo_GUI";
    
    std::lock_guard<std::mutex> lock(nombreMutex);
    miNombreGlobal = nombre.toStdString();
}

