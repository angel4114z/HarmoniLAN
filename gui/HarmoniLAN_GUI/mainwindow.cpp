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
#include <map>
#include <cstdlib>
#include <ctime>

#define RECEIVER_PORT 5000 
#define FRAME_SIZE 960

struct AudioFrame {
    std::vector<int16_t> samples;
};

struct UserAudioStream {
    OpusDecoder* decoder;
    std::queue<AudioFrame> jitterBuffer;
};

std::map<uint32_t, UserAudioStream> activeStreams;
std::mutex streamsMutex;
int receiverSockfd;

static int receiverAudioCallback(
    const void *inputBuffer, void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags, void *userData
) {
    int16_t *out = (int16_t*)outputBuffer;
    std::lock_guard<std::mutex> lock(streamsMutex);

    std::vector<int32_t> mixBuffer(FRAME_SIZE, 0);

    for (auto it = activeStreams.begin(); it != activeStreams.end(); ++it) {
        if (!it->second.jitterBuffer.empty()) {
            AudioFrame frame = it->second.jitterBuffer.front();
            it->second.jitterBuffer.pop();
            for (int i = 0; i < FRAME_SIZE; i++) {
                mixBuffer[i] += frame.samples[i];
            }
        }
    }

    for (int i = 0; i < FRAME_SIZE; i++) {
        int32_t sample = mixBuffer[i];
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        *out++ = (int16_t)sample;
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
uint32_t my_ssrc = 0; // SSRC Dinámico (Se inicializará luego)

std::atomic<bool> enviarAudio(false);

std::string miNombreGlobal = "YoGUI";
std::mutex nombreMutex;

// Variables Globales para SALA HOST
std::atomic<bool> esHostGlobal(false);
std::mutex clientesMutex;
// Usaremos la IP como String y la estructura sockaddr_in para reenviar
std::map<std::string, sockaddr_in> clientesEnSala;

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
        header.ssrc = htonl(my_ssrc);
            
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

    srand(time(nullptr));
    if (my_ssrc == 0) my_ssrc = rand(); // SSRC Aleatorio para evitar colisiones
    
    buscandoEquipos = false;
    discoveryTimer = new QTimer(this);
    discoveryTimer->setSingleShot(true);
    connect(discoveryTimer, &QTimer::timeout, this, &MainWindow::finDescubrimiento);
    
    connect(ui->lstUsuarios, &QListWidget::itemClicked, this, &MainWindow::onUsuarioSeleccionado);

    udpControlSocket = new QUdpSocket(this);
    connect(udpControlSocket, &QUdpSocket::readyRead, this, &MainWindow::procesarRespuestaUDP);

    receiverSockfd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in recAddr{};
    recAddr.sin_family = AF_INET;
    recAddr.sin_port = htons(RECEIVER_PORT);
    recAddr.sin_addr.s_addr = INADDR_ANY;
    ::bind(receiverSockfd, (sockaddr*)&recAddr, sizeof(recAddr));

    // Lanzamos tu hilo nativo de red (Jitter Buffer Múltiple)
    std::thread receiverNetworkThread([this]() {
        while (true) {
            char packet[4096];
            sockaddr_in senderAddr{};
            socklen_t senderLen = sizeof(senderAddr);
            int bytesReceived = recvfrom(receiverSockfd, packet, sizeof(packet), 0, (sockaddr*)&senderAddr, &senderLen);
            if (bytesReceived <= (int)sizeof(RTPHeader)) continue;

            RTPHeader header{};
            memcpy(&header, packet, sizeof(RTPHeader));
            uint32_t sender_ssrc = ntohl(header.ssrc);

            // Evitamos hacer eco reproduciendo nuestro propio audio
            if (sender_ssrc == my_ssrc) continue;
            
            // Si somos Host, reenviamos el paquete recibido al resto de usuarios registrados
            if (esHostGlobal.load()) {
                char ipRec[INET_ADDRSTRLEN];
                sockaddr_in sAddr;
                socklen_t sLen = sizeof(sAddr);
                getpeername(receiverSockfd, (sockaddr*)&sAddr, &sLen);
                inet_ntop(AF_INET, &senderAddr.sin_addr, ipRec, sizeof(ipRec));
                std::string currentIp(ipRec);

                std::lock_guard<std::mutex> clLock(clientesMutex);
                
                // Registramos automáticamente a quien nos envíe audio en el puerto 5000 (Mágica conexión SFU)
                if (clientesEnSala.find(currentIp) == clientesEnSala.end()) {
                    clientesEnSala[currentIp] = senderAddr;
                }

                // Reenviar a todos menos a quien lo envió
                for (auto const& [ipDest, destAddr] : clientesEnSala) {
                    if (ipDest != currentIp) {
                        sendto(receiverSockfd, packet, bytesReceived, 0, (sockaddr*)&destAddr, sizeof(destAddr));
                    }
                }
            }

            unsigned char *opusData = (unsigned char*)(packet + sizeof(RTPHeader));
            int opusSize = bytesReceived - sizeof(RTPHeader);

            std::lock_guard<std::mutex> lock(streamsMutex);

            // Si es un usuario nuevo, le creamos su propio decodificador
            if (activeStreams.find(sender_ssrc) == activeStreams.end()) {
                int err;
                UserAudioStream newStream;
                newStream.decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
                activeStreams[sender_ssrc] = newStream;
            }

            std::vector<int16_t> pcm(FRAME_SIZE);
            int decodedSamples = opus_decode(activeStreams[sender_ssrc].decoder, opusData, opusSize, pcm.data(), FRAME_SIZE, 0);

            if (decodedSamples > 0) {
                activeStreams[sender_ssrc].jitterBuffer.push({pcm});
                if (activeStreams[sender_ssrc].jitterBuffer.size() > 50) {
                    activeStreams[sender_ssrc].jitterBuffer.pop();
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
                        
                        // Si somos Host, respondemos distinto para que se pongan como Sala
                        std::string prefix = esHostGlobal.load() ? "ROOM_HOST:" : "DISCOVER_RESPONSE:";
                        std::string response = prefix + nombreParaEnviar;
                        
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
    
    // Destruir decodificadores de todos los usuarios
    {
        std::lock_guard<std::mutex> lock(streamsMutex);
        for (auto& pair : activeStreams) {
            if (pair.second.decoder) {
                opus_decoder_destroy(pair.second.decoder);
            }
        }
    }
    
    ::close(receiverSockfd);
    
    delete ui;
}

void MainWindow::on_btnBuscar_clicked()
{
    ui->lstUsuarios->clear();
    ui->txtLogs->append("Buscando usuarios...");
    
    // Al buscar cancelamos si hubiésemos sido hosts
    esHostGlobal.store(false);

    buscandoEquipos = true;
    
    QString nombre = ui->txtNombreUsuario->text().trimmed();
    if (nombre.isEmpty()) nombre = "Anonimo_GUI";
    
    QByteArray mensaje = "DISCOVER:" + nombre.toUtf8();

    udpControlSocket->writeDatagram(mensaje, QHostAddress::Broadcast, 5001);

    discoveryTimer->start(2000);
}

void MainWindow::on_btnCrearSala_clicked()
{
    QString nombre = ui->txtNombreUsuario->text().trimmed();
    if (nombre.isEmpty()) nombre = "MiSala";

    esHostGlobal.store(true); // ¡Activamos el modo Relay/SFU!
    
    // Limpiamos los clientes previos que hubiera
    {
        std::lock_guard<std::mutex> lock(clientesMutex);
        clientesEnSala.clear();
    }
    
    ui->lstUsuarios->clear();
    ui->txtLogs->append("¡Has creado la sala [" + nombre + "]!");
    ui->txtLogs->append("Estás en modo Host. Los audios se reenviarán automáticamente a todos.");
    ui->txtLogs->append("Puedes usar PTT en cualquier momento.");
    
    // Como somos el host, nos activamos el micrófono hacia la dirección loopback (opcional, o no hace falta ya que no enviamos, recibimos y reenviamos de nosotros).
    // O simplemente habilitamos el PTT (Audio se enviará cuando los demás se conecten a nosotros).
    
    ui->btnPTT->setEnabled(true);
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

        if (respuesta.startsWith("DISCOVER_RESPONSE:") || respuesta.startsWith("ROOM_HOST:")) {
            QString ipDetectada = datagram.senderAddress().toString();
            if (ipDetectada.startsWith("::ffff:")) ipDetectada = ipDetectada.mid(7);
            
            bool isRoom = respuesta.startsWith("ROOM_HOST:");
            QString nombreUsuario = isRoom ? respuesta.mid(10) : respuesta.mid(18);

            QString textoElemento = (isRoom ? "[SALA] " : "") + nombreUsuario + " (" + ipDetectada + ")";
            QListWidgetItem *item = new QListWidgetItem(textoElemento, ui->lstUsuarios);
            item->setData(Qt::UserRole, ipDetectada);
            
            if (isRoom) {
               item->setForeground(Qt::blue);
               QFont font = item->font();
               font.setBold(true);
               item->setFont(font);
            }
            
            ui->txtLogs->append("Encontrado: " + textoElemento);
        }
    }
}

void MainWindow::onUsuarioSeleccionado(QListWidgetItem *item)
{
    // Al unirte a una persona o sala, tú no eres Host
    esHostGlobal.store(false);

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

