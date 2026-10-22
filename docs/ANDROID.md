# Android ARM64

Aplicación nativa C++ con Qt Widgets. No utiliza Termux ni emulador como runtime.
Requiere Android 9/API 28 o posterior y ABI `arm64-v8a`.

## Construcción

Instalar CMake >=3.22, Ninja, Git, JDK 21, Android SDK y NDK 27.2.12479018.
El SDK necesita `platform-tools`, `platforms;android-35`, `platforms;android-36`,
`build-tools;35.0.0` y `build-tools;36.0.0`. Qt 6.11 compila Java con SDK 36;
el APK mantiene mínimo API 28 y target API 35.

```bash
sdkmanager 'platform-tools' 'platforms;android-35' 'platforms;android-36' \
  'build-tools;35.0.0' 'build-tools;36.0.0' 'ndk;27.2.12479018'
```
Instalar Qt **6.11.2 para Android arm64-v8a**, incluyendo Qt Multimedia y las
herramientas host de la misma versión. Se puede usar el instalador oficial o
construir Qt Base, ShaderTools y Multimedia con `scripts/build-qt-source.sh` (descarga fuentes
oficiales, sin copiarlas al repositorio). ShaderTools aporta qsb para construir
el módulo Multimedia completo. No hacen falta Qt Quick/QML ni FFmpeg
para QAudioSink/AAudio.

```bash
export JAVA_HOME=/path/to/jdk-21
export ANDROID_SDK_ROOT=/path/to/android-sdk
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
export QT_ANDROID_PREFIX=/path/to/Qt/6.11.2/android_arm64_v8a
export QT_HOST_PATH=/path/to/Qt/6.11.2/gcc_64
./scripts/build-android.sh
```

CMake descarga y compila las dependencias portables con el mismo NDK:
fmt, nlohmann/json, libzip y libsndfile; Boost sólo aporta cabeceras.
Zlib procede del Android NDK.
Se desactivan codecs y herramientas externas innecesarios, conservando exportación
WAV float/PCM. No utiliza pkg-config host al compilar Android.

El script conserva C++ optimizado (`Release`) y fuerza
`QT_ANDROID_DEPLOYMENT_TYPE=Debug` para generar un APK de desarrollo firmado
con la clave debug local. El resultado está en
`build-android/artifacts/agbplay-android-arm64-debug.apk`. Nunca subir esa clave. Para distribuir una versión estable, firmar el
APK de release con una clave propia y conservarla fuera de Git. Las claves
debug de runners efímeros pueden cambiar entre compilaciones. Si Android rechaza
la actualización por firma distinta (`INSTALL_FAILED_UPDATE_INCOMPATIBLE`),
no puede instalarse encima del primer APK: guardar los perfiles/exportaciones
que se necesiten y desinstalar la versión anterior antes de instalar (se borran
sus datos privados). No se publica ninguna clave en Git.

```bash
adb install -r build-android/artifacts/agbplay-android-arm64-debug.apk
./scripts/android-smoke.sh build-android/artifacts/agbplay-android-arm64-debug.apk
```

## Uso y almacenamiento

Abrir ROM/GSF usa el selector de documentos del sistema de Qt. Desde la versión
1.0.2, las URI `content://` se leen directamente con
`ContentResolver.openInputStream` de Android, conservando su codificación y
consultando opcionalmente `OpenableColumns.DISPLAY_NAME`. No se reconstruye
la ruta ni se depende del lector de archivos de Qt para acceder al proveedor.
La copia usa bloques de 64 KiB y un máximo de 64 MiB por documento, leyendo hasta
EOF sin suponer un tamaño conocido, buscar en el flujo ni consultar available().
Se publica atómicamente en caché privada sólo tras una lectura completa; un
fallo elimina el temporal, conserva la copia previa y deja abierto el juego
actual. Los errores JNI muestran la excepción concreta de Android.
El motor sigue usando sus lectores originales de archivos/ZIP. Los perfiles y
configuración se guardan mediante QStandardPaths en el espacio privado de la app.
No se pide permiso de acceso general al almacenamiento.

Se conservan listas, playlist, play/pause/stop, velocidad, volumen, mute/solo,
perfiles y configuración. El botón permanente **Abrir ROM/GSF** usa la misma
acción que File > Open ROM/GSF. En Android se fuerza el diálogo nativo de Qt,
que lanza `ACTION_OPEN_DOCUMENT` (SAF); la opción inicial permite cualquier tipo
MIME para que los proveedores no oculten extensiones GBA/GSF desconocidas.

La distribución usa el tamaño disponible del área central en coordenadas lógicas
Qt, sin detectar categorías de dispositivo:

- ancho menor de 840 o alto menor de 480: cinco pestañas (Canciones, Playlist,
  Pistas, ROM, Log), con apertura, transporte, velocidad y volumen permanentes;
- ancho de 840 a 1199 y alto de al menos 480: listas simultáneas a la izquierda,
  pistas/información/log en pestañas a la derecha;
- ancho de al menos 1200 y alto de al menos 480: listas, pistas e información/log
  en tres paneles redimensionables con separadores táctiles.

Rotar o cambiar el tamaño mueve los mismos widgets, conservando listas,
selección y estado de reproducción. Los grupos de apertura, transporte y niveles se organizan en tres filas si su
ancho es menor de 600; dos entre 600 y 999; una desde 1000. Las pistas muestran
M/S/A de 48 unidades,
datos técnicos y teclados que se ajustan al ancho, sin cambiar la síntesis.
Las listas tienen filas de 48 unidades, casillas ampliadas y menú de acciones
para reproducir, renombrar y subir/bajar entradas. El `+` sigue añadiendo a la
playlist. **Menú** permite acceder a todos los menús originales, incluidos
perfiles, exportación, preferencias y registro. Los formularios complejos tienen
desplazamiento táctil. Escritorio conserva su interfaz predeterminada.

Las exportaciones WAV y stems se generan en el directorio privado de exportación.
`File > Share Exported WAV` abre el selector de compartir de Android mediante
FileProvider y un permiso de lectura temporal. La selección de una carpeta SAF
para exportar directamente no está implementada. La importación automática de
MINIGSF vecinos de un GSFLIB SAF aislado no puede recorrer la carpeta del proveedor;
preferir un ZIP con el conjunto completo o importación manual.

## Pruebas reproducibles

En Linux instalar Qt6 Widgets/Multimedia (para GUI), PortAudio C++, libzip,
libsndfile, zlib, Boost, fmt y nlohmann-json. Ejemplo Debian:

```bash
sudo apt-get install cmake ninja-build g++ qt6-base-dev qt6-multimedia-dev \
  portaudio19-dev libportaudiocpp0 libzip-dev libsndfile1-dev zlib1g-dev \
  libboost-dev libfmt-dev nlohmann-json3-dev
./scripts/test-host.sh
AGBPLAY_BUILD_DIR="$PWD/build-asan" ./scripts/test-host.sh -DENABLE_ADDRESS_SANITIZER=ON
cmake -S . -B build-desktop -G Ninja -DAGBPLAY_BUILD_CLI=OFF -DAGBPLAY_BUILD_NC=OFF
cmake --build build-desktop
```

Para ejecutar la regresión de interfaz y guardar capturas sin dispositivo:

```bash
AGBPLAY_BUILD_DIR="$PWD/build-ui" AGBPLAY_UI_CAPTURE_DIR="$PWD/build-ui/captures" \
  ./scripts/test-host.sh -DAGBPLAY_BUILD_GUI=ON
```

`touch-ui` comprueba el botón de apertura, su separación del `+`, la conservación
de estado durante 27 cambios de tamaño, geometría de controles de 48 unidades,
vista de pistas sin desbordamiento horizontal y mute/solo con pestañas ocultas.
Las capturas son renderizados Qt Widgets en Linux; no son capturas de Android.

Para compilar también la salida Qt en Linux: `-DAGBPLAY_QT_AUDIO=ON` (Qt >=6.8).
La configuración de escritorio conserva PortAudio como backend predeterminado.

## Audio en Android y telemetría

Qt Multimedia 6.11 usa `QAudioSink` con su callback `QSpan<float>` sobre AAudio.
El callback consume float32 estéreo intercalado de la cola SPSC; el motor
MP2K/M4A y sus remuestreadores son los mismos que en PortAudio. La síntesis se
ejecuta en un productor independiente del event loop y solicita prioridad
`THREAD_PRIORITY_AUDIO` de Android. En modo callback Qt define los tamaños de
bloque y `setBufferSize()` no se usa; Qt solicita baja latencia y capacidad
AAudio de 1024 frames.

Desde 1.0.4 el productor precarga antes de abrir/reanudar el dispositivo y
mantiene una reserva mínima de 30 ms (1440 frames a 48 kHz), reponiendo desde
un umbral de 25 ms con esperas monotónicas calculadas. Los microbloques del
motor no cambian. El callback no usa mutexes ni notifica al productor; el
visualizador no puede bloquear la publicación de PCM. El reporte Qt de
bufferSize no representa el buffer nativo de AAudio en modo callback.

Cada cinco segundos la pestaña **Log** reporta underruns, frames faltantes,
ocupación/objetivo de cola, frecuencia/tamaño/intervalo/duración de callbacks,
máximo de síntesis pura y retraso de despertar,
RMS/pico de las muestras finales, clipping/no finitos, formato de QAudioSink y
buffer reportado. RMS/clipping se analizan en el productor desde una captura
preasignada; se cuentan las pérdidas de telemetría sin afectar al audio.
La escala del control de volumen es cuadrática y 100% equivale
a ganancia 1.0; no se aumenta la señal ni se reemplaza ningún remuestreador.
Consultar [el análisis de audio](AUDIO-ANDROID.md) para conocer las mediciones
de la ROM sintética de escritorio y sus límites.

Para investigar una distorsión reproducible en un teléfono, deja sonar la misma
canción 60–120 segundos sin tocar, abre la pestaña **Log** y copia las líneas
`Qt audio output`, `Android synthesis thread`, `Android audio`, `Android timing`
y `Android PCM`. Los
underruns miden discontinuidad de cola; RMS, pico, clipping y valores no finitos
ayudan a comprobar por separado amplitud y muestras. Falta comparar la ROM y
sus ajustes exactos entre el teléfono y escritorio.

Las pruebas utilizan datos sintéticos, sin ROMs comerciales:

- validación de cabecera, límites, overflow de offsets y entradas inválidas;
- síntesis PCM finita (la fixture de escritorio a 48 kHz registra RMS 0.0373691
  y pico 0.332435 en 240000 valores de canal), secuencias GOTO sin espera y cola
  FIFO concurrente con contadores de underrun;
- cambios de canciones, mute, velocidad, volumen, stop, suspend/resume y destrucción;
- lectura/liberación repetida de ZIP y exportación WAV float estéreo;
- comparación de los remuestreadores seleccionados con la implementación escalar.
- callbacks exactos de 96 frames / 48 kHz en simulación, bloques variables,
  retrasos de despertar/síntesis y diez minutos simulados sin interacción;
- comparación bit a bit del transporte PCM de SINC/BLEP/BLAMP, condiciones de
  cola llena/vacía, precarga, fallo de apertura y reanudación.

## Regresión del acceso a documentos Android

```bash
# JDK 17+; siete casos de copia secuencial sin dependencias Android
./scripts/test-documents.sh

# SDK 35/build-tools 35.0.0, JDK y un emulador/dispositivo de prueba API 29+
./scripts/build-saf-tests.sh
./scripts/test-saf-android.sh
```

El segundo script instala `org.agbplay.saftest`, una app de prueba separada de
agbplay que contiene únicamente datos generados. No abre ROMs comerciales.
Prueba ContentResolver con pipes, tamaño desconocido, nombres Unicode/espacios/
`#`/`%`, ID codificado con barras, metadatos opcionales, reemplazo atómico,
conservación de la copia previa ante error y permisos denegados. Incluye una
entrada propia del proveedor MediaStore Downloads del almacenamiento interno,
y elimina esa entrada al terminar. CI ejecuta estos casos en Android API 35
x86_64; el APK de agbplay se construye aparte para ARM64. El emulador se utiliza
sólo para pruebas, nunca como dependencia de la aplicación.

## Validación en dispositivo pendiente

Comprobar con un teléfono real: importación desde varios proveedores SAF,
perfiles persistentes, ROM corrupta, cambios rápidos entre canciones buenas/malas,
exportación y compartir, llamadas/foco de audio, Bluetooth/USB/auriculares,
Home/resume, bloqueo/desbloqueo, rotación, cierre por presión de memoria y
consumo prolongado. Recoger los contadores de audio y comparar grabaciones con
escritorio a igual ROM, perfil y frecuencia. Un APK compilado no demuestra esas
pruebas.

Para verificar un APK sin instalarlo:

```bash
python3 scripts/verify-apk.py path/to/agbplay-gui-debug.apk
"$ANDROID_SDK_ROOT/build-tools/35.0.0/apksigner" verify --verbose path/to/agbplay-gui-debug.apk
```

El primer comando comprueba bibliotecas exclusivamente AArch64 y alineación ELF
de 16 KiB. El segundo verifica la firma. Ninguno ejecuta el audio o la interfaz.
