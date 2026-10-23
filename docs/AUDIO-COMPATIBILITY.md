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
