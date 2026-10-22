# Validación de la entrega

## Interfaz adaptable y apertura SAF (segunda entrega)

[Descargar ZIP con el nuevo APK ARM64 firmado](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39721356692/artifacts/12401496170)

- [Workflow completo aprobado](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39721356692): interfaz, motor normal/sanitizado y construcción Android.
- SHA-256 del APK: `23fa0e04d4fe08d330777a8563aef48d0e0da3d453fa28371bdd65b0101ce9dc`.
- Firma v2 válida, un firmante; 17 bibliotecas AArch64 con LOAD alineados a 16 KiB.
- Mínimo API 28, target API 35, compile SDK 36; Qt 6.11.2/Multimedia y NDK 27.2.
- [Capturas de interfaz generadas por CI](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39721356692/artifacts/12401600463) (Qt Linux, no dispositivo Android).

Código de esta actualización: `639ac7cd906f1d9f83d933860ece7df2b33b9ffd`.
APK `1.0.1-android-dev`, versionCode **2**. La síntesis y el backend de audio no
se modificaron respecto a la primera entrega.

Se añadió un botón permanente «Abrir ROM/GSF» que dispara la misma QAction de
apertura del menú. `QFileDialog` usa AcceptOpen/ExistingFile y diálogo nativo;
se comprobó en el código de Qt 6.11.2 (`qandroidplatformfiledialoghelper.cpp`)
que esta combinación lanza `ACTION_OPEN_DOCUMENT`. El filtro inicial permite
MIME desconocidos. La copia de `content://` a caché privada sigue usando la
implementación existente. El `+` conserva PlaylistAdd().

La distribución responde al tamaño del área central: compacta por debajo de
840 de ancho o 480 de alto; dos paneles desde 840×480; tres desde 1200×480.
Los grupos de controles se distribuyen en tres, dos o una fila según su ancho.
Las vistas se mueven entre pestañas/paneles sin reconstruirse. Se conserva el
estado de pistas aunque su pestaña esté oculta; mute/solo consulta la visibilidad
propia de cada pista, independientemente de la visibilidad de sus ancestros.

La regresión gráfica también pasa en CI con Qt 6.4 de Ubuntu 24.04; las dos
configuraciones de pruebas del motor (normal y sanitizada) pasan en CI.

Comprobado localmente con Qt 6.8.2 en Linux:

- 5/5 pruebas CTest: `touch-ui`, `safety`, `playback`, `fileio`, `resampler-parity`.
- Apertura y `+` ejecutan acciones distintas; todos los menús originales están
  disponibles desde el botón táctil Menú.
- 27 cambios de tamaño entre 320×480/640, 360×740, 412×840, 740×360, 840×700,
  1024×768, 1280×800 y 600×900: conservación de selección/contenido/mute,
  controles de al menos 48 unidades dentro de sus límites y sin solapamientos.
- Capturas offscreen de teléfono, pistas, horizontal y tablet de dos/tres paneles,
  revisadas visualmente. No son capturas Android.
- 4/4 pruebas del motor con ASan, UBSan y LeakSanitizer sin supresiones.
- La prueba gráfica pasa ASan/UBSan; LeakSanitizer detecta 160 bytes en 10
  recognizers de `QScroller::grabGesture` de la biblioteca Qt 6.8.2 del host.
  Se reproduce con un programa mínimo sin agbplay (abajo), incluso llamando
  explícitamente a ungrabGesture. Con una supresión limitada a ese símbolo,
  5/5 pruebas pasan manteniendo activada la detección del resto de fugas.
  No se afirma que la prueba gráfica esté libre de fugas sin esa excepción.

Reproductor mínimo de la reserva en Qt 6.8.2:

```cpp
#include <QApplication>
#include <QScroller>
#include <QWidget>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QWidget target;
    QScroller::grabGesture(&target, QScroller::TouchGesture);
    QScroller::ungrabGesture(&target);
}
```

Compilar con `-fPIC -fsanitize=address,undefined` y las opciones de
`pkg-config --cflags --libs Qt6Widgets`; ejecutar con `QT_QPA_PLATFORM=offscreen`.
LeakSanitizer informa de 16 bytes en QScroller::grabGesture por widget. La
excepción local se configura con un archivo que contiene únicamente
`leak:QScroller::grabGesture` y `LSAN_OPTIONS=suppressions=/ruta/archivo`.
El APK utiliza Qt **6.11.2**, no la biblioteca Qt 6.8.2 de estas pruebas host.

El usuario confirmó que el primer APK abre en un teléfono real y reportó los
problemas de acceso a archivos y distribución que motivan esta actualización.
Este entorno no dispone de un teléfono conectado: todavía se requiere probar
el nuevo selector SAF, rotación, densidades y audio en un dispositivo real.

## Primera entrega: APK firmado

Código de reproducción original de esta entrega: `f26c022d0c05439ca09856fd0c17e9ce0d163749`.

[Descargar el ZIP con el APK ARM64 firmado](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39717872013/artifacts/12399534171)

- APK: `agbplay-android-arm64-debug.apk`.
- Commit de construcción/empaquetado: `59382e440950c3e0e5dafa9c29d88fdc7632eade`.
- [Workflow aprobado](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39717872013).
- SHA-256 del APK: `f56f4dec854eb36b7a473d6de471668157dcdb9402f8146484d2634c8cf2d92f`.
- 17 bibliotecas AArch64, todas con segmentos LOAD alineados a 16 KiB.
- `apksigner verify --verbose`: firma v2 válida, un firmante.
- Package `org.agbplay.android`, versión `1.0.0-android-dev`, mínimo API 28,
  target API 35, compile SDK 36.

Las primeras compilaciones generaron release sin firma. El script ahora fuerza
`QT_ANDROID_DEPLOYMENT_TYPE=Debug` y sólo publica el artefacto después de verificar
ABI, alineación y firma. El código nativo sigue compilado con optimización Release.
El artefacto de Actions tiene retención de 90 días; se reconstruye con los scripts
y todo el código fuente permanece en GitHub.

## Comprobado

| Comprobación | Resultado |
|---|---|
| Motor NDK 27.2, Clang 18, libc++, API 28 | Compilado localmente, ELF AArch64, segmentos LOAD de 16 KiB |
| APK Qt 6.11.2/Multimedia, JDK 21, compile SDK 36 / target 35 | Compilado, firmado, verificado y publicado en GitHub Actions |
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
