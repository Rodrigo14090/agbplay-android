# Validación de la entrega

Código del reproductor: `f26c022d0c05439ca09856fd0c17e9ce0d163749`.
Los commits posteriores de documentación/scripts conservan ese código.

[Compilación y artefacto ARM64 en GitHub Actions](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39716747975)
— artefacto `agbplay-android-arm64-debug`, compilado desde ese commit.

## Comprobado

| Comprobación | Resultado |
|---|---|
| Motor NDK 27.2, Clang 18, libc++, API 28 | Compilado localmente, ELF AArch64, segmentos LOAD de 16 KiB |
| APK Qt 6.11.2/Multimedia, JDK 21, SDK 35 | Compilado y publicado en GitHub Actions |
| Qt6 Widgets con PortAudio en Linux | Compilado; arranque offscreen sin ROM |
| Qt6 Widgets con Qt Multimedia 6.8 en Linux | Compilado; arranque offscreen sin ROM |
| `safety`, `playback`, `fileio`, `resampler-parity` | 4/4 aprobadas localmente y en CI |
| ASan + UBSan + LeakSanitizer | 4/4 aprobadas, sin diagnósticos de memoria/UB/fugas |
| Java AudioFocus contra Android API 35 | Compilado con javac 21 |
| Interfaz/JNI Android | Compilada mediante NDK y en el APK de CI |
| Remuestreo AVX2 frente a escalar | SINC/BLEP/BLAMP, razones 0.5/1/1.7/4.2, máximo error < 5e-7 en la señal sintética |

Las pruebas incluyen: 64 mutaciones reproducibles de tablas/secuencias/samples,
entradas 0xAAAAAA, banco nulo, índice de player inválido, bucles GOTO sin espera,
valores de remuestreo infinitos/NaN/excesivos, síntesis PCM con energía no nula,
FIFO concurrente de 100000 frames, 20 ciclos de creación/destrucción del worker,
cambios de canciones/controles y cierre sin callbacks, 50 cargas ZIP y WAV float
estéreo a 48 kHz. Las pruebas tienen timeout explícito.

LeakSanitizer no funciona bajo el ptrace del sandbox de Cloud. Se repitió fuera
de ese trazado, manteniendo activada la detección de fugas. Durante compilaciones
concurrentes pesadas se ajustaron los límites de tiempo de `safety`/`fileio` a
120 segundos; no se eliminó ningún control de memoria para hacerlas pasar.

## Límites de esta verificación

No se instaló ni reprodujo audio en un dispositivo Android real. No se ejecutó
un emulador Android. Compilar el APK y verificar su firma/ABI no garantiza que
los proveedores SAF, foco de audio, auriculares, Bluetooth, rotación y todas las
pantallas se comporten correctamente en un teléfono. Esas comprobaciones están
listadas en `ANDROID.md`, junto con los comandos de instalación y smoke test.

No se descargaron las ROMs citadas en #96. Los casos inválidos se modelaron con
datos sintéticos; no se afirma que se hayan reproducido o resuelto todos los
hacks afectados. La fidelidad numérica comprobada usa señales sintéticas, sin
medir aún la salida acústica ni latencia física.

Cloud permitió descargar SDK/NDK y compilar Qt Base ARM64 desde las fuentes
oficiales. Bloqueó download.qt.io; la construcción completa del APK usó el
workflow de GitHub, donde sí se instaló Qt 6.11.2 oficial con aqt. El script
alternativo de Qt incluye Base, ShaderTools y Multimedia; esa cadena completa
desde fuentes no se ejecutó de principio a fin en Cloud.

Pendiente: pruebas físicas, ajuste visual en varias densidades, restauración
tras terminación por Android, reproducción en segundo plano con servicio,
reapertura automática tras fallos de dispositivo, y exportación directa a un
árbol SAF. Los WAV privados se pueden compartir con FileProvider.
