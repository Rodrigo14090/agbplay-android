# Investigación de rutas Android (experimental)

## Referencia estable y alcance

Base inmutable: `android/arm64-native` en
`6ca136e06e758d9c9d441d2c013d893214d868b8`. `main` estaba en
`0b87da48d2502da359e45718eec8566ac40fa9d7`. Todo el experimento vive en
`android/audio-compatibility`; no se fusiona ni actualiza la rama estable.

## Evidencia del código, antes de implementar

Se inspeccionaron las fuentes oficiales de Qt Multimedia **6.11.2**, la versión
que instala el workflow. `QAudioSink::start(QSpan<float>)` llega directamente al
callback de `QAndroidAudioSinkStream`, que utiliza **AAudio**, no MediaPlayer.

- `src/multimedia/android/qandroidaudiosink.cpp`: salida compartida, rol Other /
  MediaPlayback con USAGE_MEDIA / CONTENT_TYPE_MUSIC. Son también los valores
  predeterminados de AAudio; por eso el builder no repite esos setters.
- `qaaudiostream.cpp`: solicita PERFORMANCE_MODE_LOW_LATENCY. Pide capacidad
  1024 frames y ajusta el tamaño nativo a tres bursts. El tamaño que informa
  QAudioSink para su ring buffer no demuestra el tamaño del stream nativo.
- Su setter de MMAP AUTO sólo se ejecuta desde API 36. En Android anteriores
  la decisión de MMAP depende de AAudio / la política del dispositivo. Ni la
  solicitud de baja latencia ni el código prueban que el dispositivo la conceda.
- No solicita un session ID de efectos (AAudio predetermina SESSION_ID_NONE).
  La MediaSession del servicio controla transporte y foco; su token no es el
  audio session ID del stream PCM.
- `qaudiosink.h` no expone selección de performance mode, MMAP ni session ID.
  Cambiar atributos del AudioFocusRequest del servicio no cambia los del sink.

AAudio puede utilizar MMAP cuando hardware/política lo permiten o una ruta
legacy interna a AudioTrack/AudioFlinger cuando no. Bluetooth y otros dispositivos
pueden cambiar esa decisión. La API AAudio no demuestra MMAP ni evita
necesariamente el mezclador/efectos. La alternativa Java AudioTrack hace
solicitudes diferentes (performance NONE y sesión generada), pero Android podría
resolver ambas hacia una misma mezcla física. Esa coincidencia sólo se puede
comprobar en el dispositivo; las opciones no son dos alias del mismo código.

Fuentes versionadas:
[Qt sink](https://github.com/qt/qtmultimedia/blob/v6.11.2/src/multimedia/android/qandroidaudiosink.cpp),
[Qt builder](https://github.com/qt/qtmultimedia/blob/v6.11.2/src/multimedia/android/qaaudiostream.cpp),
[Qt API](https://github.com/qt/qtmultimedia/blob/v6.11.2/src/multimedia/audio/qaudiosink.h),
[AAudio](https://developer.android.com/ndk/reference/group/aaudio),
[AudioTrack](https://developer.android.com/reference/android/media/AudioTrack).

## Diagnóstico que aún requiere la MatePad

No está confirmado que Qt evite todo DSP Huawei. Las políticas del fabricante,
la mezcla normal frente a rutas rápidas, efectos de sesión/dispositivo, mejoras
por aplicación, ecualización y normalización del reproductor WAV pueden explicar
la diferencia. El ajuste de altavoces no se identifica automáticamente con Histen
para auriculares. No se activarán efectos propios ni se aumentará la ganancia.

La exportación y reproducción usan el mismo perfil de sonido MP2K, pero tienen
sample rate y límites de bucles configurables por separado. El volumen de vivo
aplica `(slider/100)^2`; la exportación no incluye ese control. Para comparar:
volumen 100 en agbplay, mismo volumen multimedia del sistema y parámetros/rate,
velocidad normal, todas las pistas activas, sin EQ/normalización en el reproductor
externo y sin confundir padding/fundido con una diferencia espectral.

Se justifica ensayar **AudioTrack streaming float32 estéreo**, MEDIA / MUSIC,
PERFORMANCE_MODE_NONE y una sesión generada por Android. Es una ruta distinta
que no requiere parchear APIs privadas de Qt. No garantiza pasar por los mismos
efectos de un reproductor externo ni habilitar Harman Kardon/Huawei Histen.

## Implementación y límites del cambio

Opciones globales > Playback > **Salida Android**:

- **Automático / Qt**: predeterminado. Misma ruta QAudioSink/callback, formato,
  ganancia y parámetros de Qt que la referencia estable.
- **Compatibilidad multimedia / AudioTrack**: AudioTrack MODE_STREAM,
  ENCODING_PCM_FLOAT estéreo, MEDIA/MUSIC, PERFORMANCE_MODE_NONE y audio session
  ID generado. No fuerza dispositivo, modo exclusivo, offload ni MMAP. Sigue
  la selección de ruta Android; no crea efectos propios ni emite una promesa
  sobre procesamiento Harman Kardon/Histen.

La preferencia se guarda como `androidAudioOutputMode` en config.json. Valores
antiguos/desconocidos eligen Automático. Se aplica al cerrar las opciones con
OK o tras Apply. Si falla la alternativa, el log identifica el fallback real a
Qt, sin cambiar silenciosamente la preferencia persistida. Para reintentar,
selecciona Automático y después AudioTrack.

`AudioOutput` sigue siendo la interfaz común. Ambos comparten ganancia, productor y AudioFrameQueue. Qt usa el callback
`RenderAudio`; AudioTrack usa la lectura disponible sin relleno de la misma
etapa de ganancia/medición. Se detiene
y une el consumidor anterior antes de crear otro. No se cambia
`AudioBufferPolicy`, la reserva normal de 30 ms, capacidad del FIFO, MP2KContext
ni los remuestreadores. El FIFO conserva `Take` para callbacks y añade
`TakeAvailable` para el escritor push. La sesión/foco/notificación/wake lock del servicio existente
controla ambos modos. La pausa conserva PCM pendiente; Stop vacía también el
búfer AudioTrack mientras está suspendido. El cambio de ruta puede introducir
una breve pausa y descarta audio ya entregado al antiguo búfer nativo, que no
puede recuperarse. No se duplica ni reinicia el motor para cambiar de salida.

AudioTrack necesita un escritor dedicado porque su API pública Java es push.
Desde **1.1.1**, reutiliza un array float y scratch fijos y solicita **hasta**
5 ms de PCM por lectura. El ritmo lo marca el espacio disponible en AudioTrack:
no usa un reloj adicional que trate de recuperar deadlines atrasados. La lectura
puede devolver menos frames o ninguno; sólo se entregan los frames reales. Si
faltan, el escritor espera una notificación del productor, sin insertar ceros
ni avanzar la rampa de volumen para muestras inexistentes. El prefill también
respeta esa disponibilidad.

La señal se arma antes de comprobar el FIFO para no perder publicaciones
concurrentes. El productor sólo despierta al escritor si éste espera PCM. La
espera y su cancelación están fuera del callback; Pause/Stop despiertan y unen
el escritor antes de destruir recursos. Qt/PortAudio nunca arman esa señal.
Se pide el mayor entre el mínimo exigido por Android y 20 ms de búfer **nativo**;
la capacidad real se registra. No se incrementó ese tamaño para esta corrección.
No se cambia la reserva de síntesis, calidad ni número de motores.

Las escrituras Java son no bloqueantes. Ante backpressure se espera en el
escritor (hasta 1 ms o señal de cierre), fuera de RenderAudio, conservando offset
y resto de cada bloque estéreo. Así un pause que precede a una escritura no
puede dejar el cierre esperando a que drene un AudioTrack pausado. El callback
Qt y RenderAudio no hacen JNI, mutexes, esperas ni asignaciones nuevas.

**Comparar WAV por esta salida…** usa SAF, copia asincrónica existente y un lector
libsndfile en el productor. Requiere abrir una ROM/GSF primero y un WAV estéreo
PCM16/24/32 o float32 a la frecuencia real de reproducción. Si cambiaste la
frecuencia de preferencias, vuelve a abrir la ROM para aplicarla. No se hace
resampling, downmix, normalización ni EQ del WAV. Se reproduce en bucle; pausa y
Stop siguen disponibles. Selecciona una canción para volver a MP2K. Los controles
de pistas/velocidad sólo tienen sentido al sintetizar MP2K, no al escuchar el
WAV ya mezclado. El visualizador de pistas no representa las notas del WAV.
La importación SAF mantiene el límite existente de **64 MiB por documento**
(aproximadamente 2 min 55 s para float32 estéreo a 48 kHz). Para un WAV mayor,
compara un tramo más corto conservando formato y frecuencia; el lector es
streaming, pero la copia privada sigue acotada por ese importador.

## Prueba A/B/C en MatePad

1. Exporta la misma canción como **WAV float32 estéreo a 48000 Hz**. Usa velocidad
   normal, todas las pistas activas y volumen agbplay 100. Iguala el volumen
   multimedia del sistema y desactiva EQ/normalización opcionales del reproductor
   externo. Anota los ajustes de sonido/altavoces de HarmonyOS; no los cambies
   entre muestras. Evita tramos de padding o fundido.
2. **A:** escucha MP2K en Automático / Qt. Espera 10 s y guarda el log de agbplay
   (File > Save Log) o toma un informe ADB con el script inferior.
3. **B:** reproduce ese WAV en el reproductor externo, por los mismos altavoces,
   y toma otro informe sin cambiar el volumen del sistema.
4. **C:** vuelve a agbplay, opciones > Comparar WAV por esta salida… y selecciona
   el mismo WAV. Escucha y registra. Alterna Automático/AudioTrack, tanto en WAV
   como en vivo. Anota qué combinación cambia y si se iguala a B.
5. Repite con Bluetooth, luego vuelve a altavoces; comprueba rutas, continuidad,
   pantalla apagada, pausa/reanudación y siguiente canción.

Interpretación: si A y C se parecen y B difiere, investiga mezcla/efectos/ruta
externos; si C se parece a B y A difiere, revisa primero parámetros/ganancia de
síntesis. Si AudioTrack modifica ambos, hay evidencia de una diferencia entre
rutas en ese dispositivo, pero todavía no identifica un DSP propietario. Para
atribuir una diferencia espectral hacen falta capturas alineadas y con nivel
igualado (loopback o medición acústica controlada), no sólo RMS del PCM interno.
La instrumentación interna no observa el sonido después del DSP/altavoces.

Con depuración USB y ADB, durante cada caso:

```sh
./scripts/collect-android-audio.sh A-live-qt
./scripts/collect-android-audio.sh B-wav-external
./scripts/collect-android-audio.sh C-wav-qt
./scripts/collect-android-audio.sh D-live-audiotrack
./scripts/collect-android-audio.sh E-wav-audiotrack
```

No se requiere root, permisos generales de almacenamiento ni modificar el DSP.
`dumpsys` puede estar limitado por el fabricante. Los informes quedan en
`audio-reports/<caso>` y pueden contener nombres de dispositivos/aplicaciones;
revísalos antes de compartirlos. No incluyas ROMs ni el WAV comercial en el PR.

El log separa **FIFO** (underrun callbacks/missing frames/RMS/pico) de
**AudioTrack nativo** (underrunCount, playbackHead, buffer/capacity, session,
performance, atributos y ruta). En la alternativa, "callback" en la telemetría
común cuenta lecturas de hasta 5 ms de PCM real, no callbacks AAudio. Desde
1.1.1, los aplazamientos por FIFO vacío y lecturas parciales se registran como
`AudioTrack push`, con espera total/máxima, separación de lecturas, backpressure
y frames aceptados. No equivalen a pérdida de muestras. `underrunCount` nativo
sigue siendo necesario: cero rellenos no demuestra cero pérdidas del dispositivo. QAudioSink no expone esos
contadores nativos; sus informes se identifican como Qt. Cambios de lista Qt y
rutas AudioTrack se registran sin afirmar que cambie el DSP. El audio session ID
de AudioTrack no es el token de MediaSession de la notificación.

En `media.audio_flinger`, correlaciona el audio session ID con el stream y las
cadenas de efectos habilitadas; en `media.audio_policy`, identifica dispositivos,
mezcladores y flags de salida (por ejemplo FAST/DIRECT/MMAP, cuando se publiquen).
Compara los casos completos, no sólo el nombre de la aplicación. La presencia
de una cadena no identifica por sí sola el ajuste de los cuatro altavoces ni
prueba su contribución acústica. Si un dump está denegado o no contiene esa
información, conserva esa limitación como resultado desconocido.

## Orden de las órdenes del servicio

El ensayo Android detectó una carrera al cambiar de vivo a WAV mediante
Stop seguido inmediatamente de Play: onDestroy del servicio anterior podía
enviar una confirmación Stop tardía que cancelaba la reproducción nueva. Las
solicitudes y confirmaciones JNI ahora llevan una generación de transporte.
Java rechaza intents atrasados y Qt descarta confirmaciones de otra generación.
Un Play explícito se confirma incluso si el servicio ya considera activa la
reproducción; recuperar foco no duplica esa confirmación. La prueba añade tres
reinicios rápidos desde la interfaz, además del cambio vivo/WAV y sus controles.
Este cambio sólo ordena el plano de control; no altera muestras ni buffering.

La investigación de microcortes y sus pruebas están en [AUDIOTRACK-BACKGROUND.md](AUDIOTRACK-BACKGROUND.md).
