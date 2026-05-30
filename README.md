# HarmoniLAN

HarmoniLAN es una aplicacion de Voz sobre IP (VoIP) para redes locales (LAN) que permite comunicaciones de audio de baja latencia. Cuenta con mecanismos peer-to-peer (tipo Walkie-Talkie) y un sistema avanzado de salas basado en una arquitectura SFU (Selective Forwarding Unit) donde un nodo actua como anfitrion y reenvia el audio a multiplicidad de usuarios.

## Requisitos de Sistema

Para compilar y ejecutar HarmoniLAN vas a necesitar las siguientes dependencias instaladas en tu sistema Linux:

- **Compilador C++**: Soporte para el estandar C++17 (gcc o clang).
- **CMake**: Version 3.10 o superior.
- **PortAudio**: Libreria para captura y reproduccion de hardware de audio (portaudio19-dev).
- **Opus**: Codec de audio de baja latencia (libopus-dev).
- **Qt5 o Qt6**: Librerias de interfaz grafica, incluyendo modulos Core, Gui, Widgets y Network (qtbase5-dev).
- **PkgConfig**: Herramienta de localizacion de bibliotecas.

En sistemas basados en Debian/Ubuntu puedes instalar las dependencias con:
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config portaudio19-dev libopus-dev qtbase5-dev
```

## Compilacion

El proyecto utiliza CMake de forma estandar. Puedes construir todos los programas de consola y la interfaz visual desde la raiz del repositorio con:

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Modo de Uso (Interfaz Grafica)

Al finalizar la compilacion, el componente principal a ejecutar es la interfaz visual:

```bash
./build/HarmoniLAN_GUI
```

### 1. Configurar tu identidad
En la parte superior izquierda de la ventana, ingresa tu nombre en la caja de texto. Si lo dejas vacio, apareceras como "Anonimo_GUI".

### 2. Comunicacion Directa (P2P)
- Haz clic en "Buscar Equipos" para solicitar descubrir miembros en red.
- En la lista derecha, selecciona a la persona a la que deseas hablar de forma privada.
- Manten pulsado el boton "PTT" para hablar.

### 3. Crear una Sala (Modo Host / SFU)
- Ingresa el nombre de la sala que desees crear en la misma caja de texto de nombre (o ingresa un identificador personal, el sistema lo etiquetara como sala).
- Haz clic en "Crear Sala".
- Has iniciado tu sistema en modo Relay. Todo aquel que busque equipos vera tu nombre marcado bajo la seccion "[SALA]".
- Tu cliente local reenviara el trafico de aquellos que hablen en tu sala hacia los demas miembros para habilitar llamadas de varios participantes en la red sin degradarla.

### Funciones adicionales
- **Habla Continua**: Activa la casilla homonima si deseas dejar el microfono siempre abierto, sin tener que presionar el boton PTT.