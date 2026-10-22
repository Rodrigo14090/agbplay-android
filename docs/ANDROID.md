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
debug de runners efímeros pueden cambiar entre compilaciones.

```bash
adb install -r path/to/agbplay-gui-debug.apk
./scripts/android-smoke.sh path/to/agbplay-gui-debug.apk
```

## Uso y almacenamiento

Abrir ROM/GSF usa el selector de documentos del sistema. Qt abre `content://`;
se copia en bloques de 64 KiB a caché privada con un máximo de 64 MiB por entrada.
El motor sigue usando sus lectores originales de archivos/ZIP. Los perfiles y
configuración se guardan mediante QStandardPaths en el espacio privado de la app.
No se pide permiso de acceso general al almacenamiento.

Se conservan listas, playlist, play/pause/stop, velocidad, volumen, mute/solo,
perfiles y configuración. En teléfonos, las listas y controles son táctiles y
las formas complejas tienen desplazamiento. La disposición visual sigue siendo
una adaptación inicial; verificar en varias densidades y orientaciones.

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

Para compilar también la salida Qt en Linux: `-DAGBPLAY_QT_AUDIO=ON` (Qt >=6.8).
La configuración de escritorio conserva PortAudio como backend predeterminado.

Las pruebas utilizan datos sintéticos, sin ROMs comerciales:

- validación de cabecera, límites, overflow de offsets y entradas inválidas;
- síntesis PCM finita, secuencias GOTO sin espera y cola FIFO concurrente;
- cambios de canciones, mute, velocidad, volumen, stop, suspend/resume y destrucción;
- lectura/liberación repetida de ZIP y exportación WAV float estéreo;
- comparación de los remuestreadores seleccionados con la implementación escalar.

## Validación en dispositivo pendiente

Comprobar con un teléfono real: importación desde varios proveedores SAF,
perfiles persistentes, ROM corrupta, cambios rápidos entre canciones buenas/malas,
exportación y compartir, llamadas/foco de audio, Bluetooth/USB/auriculares,
Home/resume, bloqueo/desbloqueo, rotación, cierre por presión de memoria y
consumo prolongado. Medir underruns, latencia y batería; comparar grabaciones
con escritorio a igual perfil/frecuencia. Un APK compilado no demuestra esas pruebas.

Para verificar un APK sin instalarlo:

```bash
python3 scripts/verify-apk.py path/to/agbplay-gui-debug.apk
"$ANDROID_SDK_ROOT/build-tools/35.0.0/apksigner" verify --verbose path/to/agbplay-gui-debug.apk
```

El primer comando comprueba bibliotecas exclusivamente AArch64 y alineación ELF
de 16 KiB. El segundo verifica la firma. Ninguno ejecuta el audio o la interfaz.
