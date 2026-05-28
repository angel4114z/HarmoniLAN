#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUdpSocket> // <-- AGREGA ESTO
#include <atomic> // <-- AGREGA ESTO al inicio

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_btnBuscar_clicked();
    void procesarRespuestaUDP(); // <-- AGREGA ESTO (Función para escuchar la IP)
    void on_btnPTT_pressed();  // <-- AGREGA ESTO
    void on_btnPTT_released(); // <-- AGREGA ESTO

private:
    Ui::MainWindow *ui;
    QUdpSocket *udpControlSocket; // <-- AGREGA ESTO (El socket de control)

    // VARIABLES DEL SENDER (Tu código de consola original)
    std::string ipObjetivo;    // Aquí guardaremos la IP que descubra el botón Buscar
    bool transmitiendo = false; // Bandera simple para controlar el PTT

};
#endif // MAINWINDOW_H