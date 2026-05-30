# Documentacion y Arquitectura de HarmoniLAN

HarmoniLAN esta diseñado como una aplicacion C++17 orientada a redes LAN, con una separacion estricta entre dos capas logicas: un protocolo de control (puerto 5001) y la infraestructura de transmision de datos en tiempo real (puerto 5000).

---

## 1. El Pipeline de Audio (Transmision y Recepcion)

El proceso mas critico de HarmoniLAN es como maneja el hardware para recoger la voz de un usuario, comprimirla y restaurarla para multiplicidad de receptores.

### Proceso de Emision (Metodo `audioCallback`)
La transmision arranca con PortAudio mediante el metodo `Pa_OpenDefaultStream`. PortAudio ejecuta la funcion estatica `audioCallback` constantemente cuando tiene datos del microfono disponibles. El proceso es el siguiente:
1. **Verificacion de Estado (PTT)**: Se consulta la variable global atomica `enviarAudio`. Si es falsa (el usuario saco el dedo del Push-To-Talk), el callback se salta inmediatamente devolviendo `paContinue` y evitando procesar el CPU o gastar red.
2. **Transformacion y Compresion**: Con los datos de audio crudos en formato PCM (`int16_t`), se invoca al metodo nativo `opus_encode()` proveyendole el encoder global, los samples brutos y un array de bytes destino. Opus achica drasticamente el peso del espectro de voz reduciendo latencia.
3. **Paquetizacion RTP**: Se instancia una estructura `RTPHeader` a la cual se le adjuntan variables de sincronizacion clave: el `sequenceNumber` para el orden de la trama, el `timestamp` y el factor de diferenciacion fundamental `ssrc` que garantiza la huella original de quien manda ese audio.
4. **Despacho UDP**: Con la instruccion `memcpy` se pega la cabecera RTP a los datos comprimidos y la funcion BSD estandar `sendto()` envia por el socket de audio crudo (`audioSockfd`) los bytes hacia la red en el puerto 5000.

### Proceso de Recepcion y Mezcla
Para recibir el material intervienen dos partes muy distintas acopladas entre si:
1. **Captura Asincrona (Hilo nativo)**: Un hilo `std::thread` (`receiverNetworkThread`) corre un ciclo infinito empleando `recvfrom()` asincrono sobre el puerto 5000. 
   - Al llegar un paquete, extrae sus bits convirtiendolos a la estructura `RTPHeader` para inspeccionar la meta-informacion y extraer el SSRC unico (`ntohl(header.ssrc)`).
   - Se consulta un Mapa global de flujos (`activeStreams`). Si este emisor jamas intervino, se le reserva matematicamente un decodificador `opus_decoder_create` exclusivo.
   - El hilo manda los bytes resultantes a `opus_decode()` y almacena las tramas analizadas de vuelta a las frecuencias PCM crudas en su respectivo *Jitter Buffer* (una estructura tipo cola o `std::queue<AudioFrame>`).
2. **Reproduccion Sincronizada (Metodo `receiverAudioCallback`)**: Al igual que el microfono, las cornetas/audifonos exigen de PortAudio informacion para sonar. 
   - El motor PortAudio inicia su retorno con un array pre-limpiado lleno de ceros.
   - Aplica un iterador sobre TODAS las colas que existan actualmente registradas en red. Las retira de su `queue` originaria y las suma entre si sobre un bus provisorio de 32-bits que previene desbordamientos de buffer por altos decibelajes.
   - Finalmente ajusta el nivel al parametro de 16-bits recortandolo a los limites \[-32768, 32767\], logrando una llamada simultanea armonica libre de crujidos.

---

## 2. Protocolo de Descubrimiento de Red (Control)

Mediante el puerto 5001 y las primitivas de `QUdpSocket` de Qt, se gestionan las entidades de forma transparente al usuario.

1. **Escaneo (Boton *Buscar*)**: Se difunde (`Broadcast: 255.255.255.255`) la cabecera `DISCOVER:[Nombre]`. 
2. **Audicion Activa**: Cada entidad corriendo la APP emplea el sub-metodo `discoveryThread`, aguardando solicitudes. Al leer la cabecera predefinida, contesta solo (`Unicast`) hacia la IP del solicitante, informando de su existencia con `DISCOVER_RESPONSE:[MiNombre]` o indicando que se trata de un nodo masivo con `ROOM_HOST:[MiNombreDeSala]`.

---

## 3. Topologia Extendida de Salas (Arquitectura Host / SFU)

Para mitigar problemas intrinsecos al escenario Mesh/Multicast y saltar hacia una solucion robusta, HarmoniLAN implemento un patron "Selective Forwarding Unit" simple.

Cuando un usuario invoca el metodo `on_btnCrearSala_clicked()`:
- La bandera atomica `esHostGlobal` enciende su modulo local de transito pasivo.
- En el receptor UDP base (recepcion de audio asincrono), se introdujo una logica derivativa en el comportamiento: Si un nuevo usuario inyecta audio, se registra su direccion BSD nativa `sockaddr_in` en el mapa `clientesEnSala`.
- Antes de ser enviados al proceso de codificacion opus local mediante `sendto`, ocurre una clonacion del elemento `packet` integro en memoria plana. Mediante un `for` iterativo, el nodo reenviara ese mismo audio identico al resto de las ips de clientes grabadas, con la unica excepcion (empleando un simple condicional `if (ipDest != currentIp)`) de esquivar enviar el rebote al emisor original evitando graves efectos de "Ghosting" o resonancia de eco.