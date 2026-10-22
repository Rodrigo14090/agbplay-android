# Reproducción Android en segundo plano

Desde 1.0.9, inicia una canción y pulsa Inicio, cambia a otra aplicación o apaga
la pantalla. La síntesis C++ y QAudioSink siguen funcionando. La notificación
multimedia y los controles de la pantalla bloqueada permiten pausar, reanudar
y detener; la sesión Android también admite siguiente/anterior.

No hay que activar un permiso de almacenamiento ni mantener la pantalla encendida.
El servicio declara `mediaPlayback`, `FOREGROUND_SERVICE`,
`FOREGROUND_SERVICE_MEDIA_PLAYBACK` y `WAKE_LOCK`. Su notificación es de sesión
multimedia; no se solicita un permiso general de notificaciones ni una excepción
manual a la optimización de batería.

## Causas del comportamiento anterior

`MainWindow` llamaba a `SuspendOutput(true)` al abandonar ApplicationActive.
Además, Qt 6.11.2 bloquea los event dispatchers al detenerse la Activity si
`android.app.background_running` no está habilitado. QAudioSink se controlaba
con un sondeo de foco de 100 ms que dependía de ese dispatcher.

Ahora la Activity declara `android.app.background_running=true`. El plano de
control Qt permanece activo para recibir los comandos de la sesión y avanzar
la playlist; las actualizaciones de la ventana se deshabilitan fuera de primer
plano y el temporizador de estado pasa de 16 a 250 ms. El visualizador no se
actualiza cuando la aplicación está oculta.
Los diálogos y resultados de importación esperan a que vuelva a primer plano;
no se crean ventanas sobre una superficie suspendida. El historial visible del
log está limitado a 2000 bloques para evitar crecimiento indefinido.

## Ciclo de vida y recursos

`PlaybackService` es un servicio Android del mismo proceso. No reproduce PCM en
Java ni sustituye al motor: mantiene una MediaSession, la notificación, el foco
y un PARTIAL_WAKE_LOCK durante la reproducción. El productor C++, la cola SPSC,
la reserva de 30 ms y el callback float32 estéreo conservan su implementación.
QAudioSink sólo recibe las órdenes explícitas de transporte; se eliminó el
segundo gestor de foco y su sondeo periódico.

Una solicitud de reproducción inicia el servicio desde la interacción del usuario.
El servicio publica la notificación foreground antes de solicitar foco (requisito
relevante desde Android 15); sólo después de obtenerlo confirma Play al hilo Qt.
JNI encola comandos en el QObject controlador, no toca el motor ni la interfaz
desde el hilo Android y nunca participa en el callback de tiempo real.

La pérdida temporal de foco pausa el motor y suspende la salida, conservando la
posición; puede reanudarse cuando vuelve el foco. La pérdida permanente o una
desconexión de auriculares pausa sin reanudar automáticamente. La pausa explícita
anula una reanudación pendiente y abandona el foco. Se libera el bloqueo de CPU
al pausar y se mantiene una sesión pausada durante un plazo nominal de cinco
minutos para permitir reanudar desde la notificación. Después se detiene el
servicio. Stop, cierre del juego, error y salida liberan servicio, notificación,
MediaSession, foco y bloqueo de CPU.

El servicio es START_NOT_STICKY y stopWithTask: no intenta reconstruir un motor
C++ inexistente tras la muerte del proceso. Salir/cerrar la aplicación o quitar
su tarea de recientes detiene la música; minimizar con Inicio o apagar la
pantalla la conserva. Un cierre forzado por Android o por el fabricante sigue
pudiendo interrumpirla. No se promete supervivencia a un proceso terminado.

## Pruebas y alcance

`scripts/test-android-background.py` usa un APK de prueba con MainWindow,
PlaybackEngine, síntesis y QAudioSink reales y un patrón musical generado legal.
No usa un backend de audio simulado. Observa callbacks, frames, RMS y pérdidas
mientras envía Inicio y suspensión de pantalla con ADB. Comprueba que el event
loop sigue vivo y que continúan llegando muestras no nulas, además de comandos
multimedia, pausa sin callbacks ni wake lock y parada sin servicio restante.

Una Activity de prueba independiente solicita foco transitorio o permanente:
se verifica la pausa, la reanudación al recuperar el foco transitorio y la ausencia
de reanudación automática después de perderlo permanentemente. Las pruebas de
interfaz, carpetas SAF, GBA/GSFLIB y PCM existentes permanecen habilitadas.

Los resultados de cada entrega, commit y APK están en [VALIDATION.md](VALIDATION.md).
La ejecución en emulador x86_64 no sustituye las pruebas del APK ARM64 en un
teléfono físico, especialmente ahorro de energía del fabricante, llamadas,
auriculares Bluetooth y reproducción prolongada con pantalla apagada. Los
underruns de un host virtual sin garantías de tiempo real se registran, sin
presentarlos como una medición de continuidad del teléfono del usuario.

## Referencias oficiales

- [Servicios foreground de reproducción multimedia](https://developer.android.com/develop/background-work/services/fgs/service-types#media).
- [Gestión de foco y requisitos Android 15](https://developer.android.com/media/optimize/audio-focus).
- [Reproducción en segundo plano y uso acotado de wake locks](https://developer.android.com/media/platform/mediaplayer/background).
- [Metadatos del manifiesto Qt para Android](https://doc.qt.io/qt-6/android-manifest-file-configuration.html).
