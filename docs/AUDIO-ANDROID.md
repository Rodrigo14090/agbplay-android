# Diagnóstico de audio Android

## Camino de reproducción

`MP2KContext::m4aSoundMain()` y `SoundMixer` siguen generando el PCM estéreo
`float` usado por escritorio y Android. `PlaybackEngine` ejecuta la síntesis en
un hilo productor; `AudioFrameQueue` es una cola SPSC sin bloqueos que conserva
el orden de las muestras. El callback de `QAudioSink` consume esa cola y aplica
el control de volumen lineal antes de entregar las muestras.

Qt 6.11 usa AAudio en Android. La documentación y el código de Qt 6.11.2
confirman que `QAudioSink::start(QSpan<float>)` llama desde el callback de audio
de AAudio, con muestras `float` intercaladas; el tamaño del bloque lo decide el
backend. `QAudioSink::setBufferSize()` no aplica a esta interfaz, por lo que ya
no se llama en la ruta de callback. Qt abre AAudio en modo de baja latencia y
solicita 1024 frames de capacidad. La aplicación pide la frecuencia configurada,
dos canales y `QAudioFormat::Float`; la apertura falla explícitamente si el
dispositivo no declara ese formato compatible. AAudio puede convertir después
al formato del hardware.

La escala de volumen de la aplicación es cuadrática: 100% equivale a `1.0`, y
QAudioSink conserva su ganancia predeterminada `1.0`. El código no convierte a
PCM entero ni cambia/remuestrea la salida después del motor. El remuestreo
MP2K/M4A y sus perfiles siguen intactos.

## Corrección de planificación y mediciones

El hilo de síntesis ahora solicita `THREAD_PRIORITY_AUDIO` (-16) mediante la API
de Android. Antes era un hilo de prioridad normal que podía perder tiempo de CPU
frente al callback AAudio; el hecho de que tocar la pantalla mejorara el sonido
es compatible con una demora del productor, aunque por sí solo no demostraba que
hubiera underruns.

Cuando la ventana **Log** está visible, la aplicación informa cada cinco
segundos:

- cantidad de callbacks con underrun y frames ausentes que fueron rellenados
  con silencio;
- ocupación mínima y máxima observada de la cola, en frames estéreo;
- mínimo y máximo tamaño de callback;
- RMS y pico de las muestras `float` después del volumen de la aplicación;
- muestras con amplitud absoluta >= 1.0 y muestras no finitas;
- tiempo máximo de una mezcla, intervalo entre inicios de bloques producidos,
  intervalo entre callbacks y duración máxima del callback.

También registra la frecuencia, canales, formato, dispositivo, tamaño de buffer
que reporta Qt y volumen de QAudioSink. La medición RMS es la energía de las
muestras finitas entregadas al backend durante la ventana; los contadores de
underrun describen continuidad y no se usan para inferir fidelidad espectral.

## Fidelidad y verificación

La cola copia exactamente los valores `float` de izquierda/derecha; no tiene
conversión de tipo, limitador ni etapa de remuestreo. El control de volumen es
el único cambio deliberado al valor de muestra antes del backend. Las pruebas
de `safety` verifican el orden y el valor del FIFO con distintos wraps, y ahora
comprueban también que un underrun se cuenta por callback y por frame. Las
pruebas `playback` y toda la suite de escritorio verifican el motor con el
backend falso y las funciones de reproducción.

La ruta de síntesis está compartida con escritorio y no se modificó. No había
una ROM proporcionada que se pudiera reproducir en ambos equipos ni un Android
físico conectado a este entorno, así que no se afirma una comparación de RMS,
pico o espectro para la canción observada. Como referencia independiente del
dispositivo, la ROM sintética de `test-safety` en escritorio (48 kHz, 600
bloques) dio RMS **0,0373691**, pico **0,332435** y **240.000** valores de canal;
el FIFO entregó las muestras en orden y el test de underrun contó cuatro frames
ausentes de forma explícita. La instrumentación permite recoger
esa medición en el teléfono sin enviar la ROM: abre **Log** mientras suena la
canción y comparte las líneas `Qt audio output`, `Android synthesis thread` y
`Android audio (5s)`. Los datos de la misma ROM y los mismos ajustes permitirán
distinguir un underrun de clipping o de una diferencia acústica posterior a
AAudio.
