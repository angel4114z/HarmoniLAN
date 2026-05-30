#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUdpSocket> // <-- AGREGA ESTO
#include <QListWidgetItem> // Necesario para la lista
#include <QTimer>
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
    
    void onUsuarioSeleccionado(QListWidgetItem *item); // Para cuando pinchas en la lista
    void finDescubrimiento();

    void on_chkHablaContinua_toggled(bool checked);
    
    void on_txtNombreUsuario_textChanged(const QString &arg1);

private:
    Ui::MainWindow *ui;
    QUdpSocket *udpControlSocket; // <-- AGREGA ESTO (El socket de control)
    QTimer *discoveryTimer;       // Timer de 2 segundos
    bool buscandoEquipos;

    // VARIABLES DEL SENDER (Tu código de consola original)
    std::string ipObjetivo;    // Aquí guardaremos la IP que descubra el botón Buscar
    bool transmitiendo = false; // Bandera simple para controlar el PTT

};
#endif // MAINWINDOW_H
