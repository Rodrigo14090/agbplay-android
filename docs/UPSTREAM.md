# Procedencia y arquitectura

Este repositorio contiene el código completo de https://github.com/ipatix/agbplay,
partiendo de `0b87da4` (Update README). Se importó mediante `git fetch`, conservando
los commits, autores y licencia originales. Los cambios Android viven en
`android/arm64-native`; `upstream` identifica el repositorio oficial.

El motor sigue en `src/agbplay`: SequenceReader interpreta MP2K/M4A, SoundMixer
mezcla PCM/PSG/reverb y Resampler conserva SINC/BLEP/BLAMP y sus filtros originales.
PlaybackEngine en `src/agbplay-util` controla el hilo de síntesis y publica audio
float estéreo a AudioFrameQueue. AudioOutput selecciona PortAudio para escritorio
o Qt Multimedia para Android. `src/agbplay-gui` conserva Qt Widgets, los editores
de perfiles, listas, pistas, visualización y exportación original.

## Investigación de reportes

- [#85](https://github.com/ipatix/agbplay/issues/85): el CMake oficial incluía
  `ResamplerAVX2.cpp` y `-mavx2` en todos los procesadores. Se compila únicamente
  en x86 y se controla la inclusión/instanciación con `AGBPLAY_HAVE_AVX2`.
  `AGBPLAY_ENABLE_AVX2=OFF` permite comprobar la misma ruta escalar en x86.
  Se sustituyó la declaración adelantada de `std::thread` por `<thread>`,
  compatible con libc++/Clang. No se cambió el algoritmo escalar ni el filtro.
- [#96](https://github.com/ipatix/agbplay/issues/96): el mantenedor reportó
  entradas `0xAAAAAA` y un puntero nulo en Unbound; no se reprodujeron las ROMs
  del reporte. El código podía dejar esperando a InvokeAsPlayer después de
  terminar el worker. Ahora las peticiones se ejecutan en el worker, las
  esperas tienen predicados y la salida notifica a los solicitantes. Los
  errores de canciones detienen sus canales y permiten seleccionar otra.
  Se validan cabeceras/tablas/rangos y se limita a 16384 comandos por pista y
  tick para evitar bucles sin avance temporal. Este límite puede rechazar
  secuencias artificialmente extensas; se diagnostica, nunca se reduce calidad.

También se corrigió la propiedad de `zip_source_t`: libzip la adquiere al abrir
el archivo. El dueño original debe liberarla sólo si falla la apertura. Se
verifican lecturas completas y se acota la descompresión GSF/ZIP.

## Concurrencia y fidelidad

Sólo el worker modifica el contexto MP2K. Los controles esperan una respuesta
acotada por un bloque de síntesis; los datos de visualización usan una copia
protegida. El callback de salida consume una cola SPSC preasignada de 32768
frames, sin mutexes, asignaciones, archivos, JNI ni síntesis. En underrun rellena
con cero. Los índices son atómicos lock-free, con acquire/release. La latencia
real depende del backend y dispositivo; no se ha medido físicamente.

Se conserva la rampa de volumen original. Android requiere float estéreo a la
frecuencia elegida; si el dispositivo no la admite, se informa del error, sin
cambiar el remuestreador. Qt 6.11+ usa `QAudioSink::start(QSpan<float>)`. La ruta
QIODevice existe sólo para comprobar el backend en escritorios Qt 6.8–6.10.

En Android se suspende la salida al abandonar la aplicación. AudioFocus.java
solicita foco de media, pausa ante pérdida/ducking y desconexión de auriculares;
el hilo Qt consulta ese estado cada 100 ms, fuera del callback de audio. No hay
servicio en primer plano ni reproducción garantizada con pantalla bloqueada.
