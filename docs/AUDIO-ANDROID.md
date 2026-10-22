# Audio Android: underruns de 1.0.3 y corrección en 1.0.4

## Evidencia del teléfono

Los registros aportados por el usuario son mediciones de un Android ARM64 real,
no pruebas sintéticas. AAudio solicita 96 frames a 48 kHz: **2 ms por callback**.
En los seis intervalos se registraron 110, 165, 106, 112, 78 y 9 callbacks sin PCM
suficiente; faltaron 8312, 11496, 7440, 7216, 5456 y 568 frames. El máximo de cola
observado al entrar al callback fue 296 frames (6,17 ms). Los intervalos entre
comienzos de producción llegaron a 10,139 ms; el máximo de trabajo de síntesis
reportado llegó a 4,932 ms. Prioridad -16, float32 estéreo, ganancia Qt 1.0,
clipping y no finitos = 0. Los dos últimos intervalos coincidieron con interacción
táctil y tuvieron menos pérdidas. Eso prueba discontinuidad de suministro; no
prueba distorsión armónica ni identifica un gobernador de CPU concreto.

## Causa comprobable en el código

La política anterior dejaba de producir al superar `outputNumBuffers *
callbackFrames`. Con el valor predeterminado 2 y callbacks de 96, el objetivo
se reducía a **192 frames / 4 ms**. Después esperaba 1 ms en un condition_variable;
la espera puede terminar varios milisegundos tarde por planificación. No había
una reserva temporal mínima ni precarga antes de iniciar el dispositivo.

El motor produce **200 frames / 4,167 ms** por `m4aSoundMain()` a 48 kHz:
`floor(48000 / (59,7275 * INTERFRAMES))`, con INTERFRAMES=4. El objetivo anterior
permitía reponer sólo uno o dos microbloques antes de volver a dormir. Un
retraso de 7–10 ms podía agotar esa reserva. Los 296 frames del registro son una
muestra *antes de leer*, no el pico exacto después de escribir. Ahora se registran
ambos valores por separado.

No hay dependencia del event loop Qt para producir PCM. La correlación táctil
es compatible con cambios en planificación/frecuencia de CPU, pero los logs no
permiten confirmar ese mecanismo del kernel. Priorizar el hilo no resolvía el
error de presupuesto: un productor con una reserva inferior a sus demoras sigue
sufriendo underruns.

También había un mutex compartido con el visualizador: el productor podía
esperar mientras la UI copiaba vectores. La publicación ahora usa `try_lock` y
omite una actualización visual si el lector está ocupado. No omite PCM.

## Política y ciclo de vida

`AudioBufferPolicy` establece para Android un **mínimo de 30 ms** de PCM; mantiene
la preferencia existente de número de buffers si ésta solicita más. A 48 kHz y
96 frames/callback, el objetivo es 1440 frames, con umbral de reposición de 1200
frames / 25 ms. El productor rellena hasta el objetivo en un lote de microbloques
originales y calcula con el PCM restante el próximo plazo de reposición.
`condition_variable::wait_until` usa el reloj monotónico; comandos, cambio de
preferencias, suspensión y cierre interrumpen la espera. El callback no notifica
condition_variables, no espera ni utiliza mutexes.

Con bloques de 200, la cola de reproducción suele moverse entre 25 y menos de
34,17 ms si el productor despierta a tiempo. Ésa es latencia de la app; se añade a
la del dispositivo. El margen se eligió frente a las demoras medidas de unos
10 ms y picos de trabajo de unos 5 ms. No promete cubrir paradas ilimitadas ni una
síntesis permanentemente más lenta que tiempo real. Los ensayos de 20, 30 y 40 ms
permiten comprobar el compromiso: con bloques de 200, 10 ms de retraso de despertar
y picos de síntesis de 5 ms, 20 ms aún perdió 120 frames en tres callbacks; 30 y
40 ms no perdieron frames. Se eligió 30 ms como margen para un reproductor musical.

La capacidad física del FIFO sigue siendo 32768 frames / 256 KiB. A 48 kHz el
objetivo de 1440 frames ocupa 11,25 KiB de PCM. No se aumentó la capacidad ni se
cambió el tamaño de bloque del mezclador. Se comprueba espacio disponible antes
de sintetizar, y todos los `Put` verifican la capacidad sin descartar PCM
producido. Los cursores SPSC tienen publicación release/adquisición acquire y
están separados en líneas de caché; ninguna parte externa reinicia el FIFO.

Antes de abrir el backend se inicia el productor y se espera a la precarga. Al
reanudar una suspensión explícita, se rellena con el dispositivo todavía
suspendido. La construcción que falla detiene y une el hilo. El cierre detiene
primero los callbacks, luego solicita fin bajo el mutex que protege la espera,
notifica y une el productor, evitando perder la notificación al suspender.
Los comandos conservan la cola ordenada; cambios, mute y pausa pueden tardar
hasta el PCM ya encolado (aproximadamente 34 ms con los valores predeterminados).

Escritorio conserva por defecto la preferencia de períodos y su espera de 1 ms;
las pruebas Linux ejecutan también explícitamente la política temporal Android.
La actualización del tamaño real de callback ahora se aplica a ambos backends.
PortAudio mantiene su conversión de muestras original.

## Callback, formato y medición

Qt 6.11.2 usa el callback de AAudio. La app solicita la frecuencia configurada,
dos canales y `QAudioFormat::Float`; los floats están intercalados L/R. No hay
conversión propia a enteros ni remuestreo posterior. El mezclador y SINC/BLEP/BLAMP
no cambian. La escala cuadrática del slider conserva 100%=ganancia 1.0. Los logs
del teléfono no justifican amplificar una salida con pico 0,47–0,54 y cero clipping.

Los 96000 bytes de `QAudioSink::bufferSize()` son el reporte del ringbuffer Qt
(de unos 250 ms a 48 kHz), **no** una medida del buffer nativo AAudio ni del FIFO
de agbplay. Qt evita ese ringbuffer al usar el callback. `setBufferSize()` sólo
se usa en la ruta QIODevice de Qt anterior a 6.11. El código de Qt solicita baja
latencia, capacidad nativa de 1024 frames y tres bursts; el tamaño real concedido
por Android no se expone por esta API pública. No se presenta como medido.

El callback se limita a leer PCM, aplicar el volumen original y copiar a una
captura SPSC preasignada. Los contadores, marcas de tiempo y actualizaciones de
máximos son operaciones lock-free; no hay I/O, asignaciones ni análisis RMS en
él. El productor analiza esa captura y publica RMS, pico, clipping y no finitos
fuera del callback. La captura añade 256 KiB sólo en Android o en pruebas que
activan diagnóstico. Si se llena, se pierde telemetría y se cuenta como tal; no
se pierde PCM de reproducción. El análisis por iteración también está acotado.
La energía se acumula como double, evitando la conversión a entero potencialmente
fuera de rango de la instrumentación anterior.

Cada informe del Log incluye:

- underrun callbacks y missing frames (sólo el FIFO de salida de la app);
- cola mínima/máxima antes de leer, pico del productor, objetivo y umbral en
  frames/ms y tamaño de microbloque;
- cantidad y frecuencia efectiva de callbacks, frames solicitados/segundo y
  frames producidos;
- máximo de síntesis pura, visualizador, trabajo del productor, intervalo entre
  comienzos de producción, retraso respecto al plazo de despertar, intervalo y
  duración del callback;
- RMS, pico, clipping/no finitos, valores analizados y frames de telemetría
  descartados. La captura puede llevar algunos milisegundos de retraso.

El intervalo entre bloques incluye la espera deliberada; **no** es el coste de
síntesis. La medida antigua `max synth` incluía también visualización y publicación;
la nueva los separa. Los contadores se reinician al consultar cada ventana. No se
mide el contador de xruns interno de AAudio porque Qt no lo expone públicamente.

## Pruebas y límites

`audio-buffering` usa el mismo FIFO y la misma política que la app. Un simulador
de eventos discretos solicita 96 frames cada 2 ms exactos, separando el coste de
síntesis de la tardanza al despertar. Verifica distintos bloques (64, 100, 200,
256, 480 y 1024 frames), retrasos de 10 ms, picos de síntesis de 5 ms, diez minutos
simulados con retrasos de 15 ms, FIFO lleno/vacío y reserva de 20–40 ms. La política
anterior reproduce pérdidas. Paradas de 45 ms y síntesis sostenida de 5 ms por
200 frames provocan pérdidas esperadas y contadas: una reserva finita no puede
compensar generación sostenida de sólo 40000 frames/s para consumir 48000.

Las pruebas concurrentes verifican orden y muestras estéreo con escrituras de
tamaño variable, rechazo de escrituras sin espacio, wraps y padding de silencio.
La prueba del trabajador real usa callbacks de 96 frames, tres segundos sin
comandos ni Qt/eventos de UI, cambios de canción, pausa, suspensión/reanudación,
preferencias, 20 vidas de engine y fallo de apertura tras precargar.
El backend falso usa hilos Linux y esperas nominales de 2 ms; el simulador es el
que exige consumo exacto de 48000 frames/s independientemente del planificador.
La prueba concurrente exige continuidad cuando los máximos observados caben en
su presupuesto (retraso de despertar <=10 ms, trabajo <=5 ms e intervalo entre
producciones <=20 ms). Fuera de él registra explícitamente las pérdidas sin
certificar continuidad; las invariantes de datos y ciclo de vida siguen siendo
obligatorias. Una repetición del host cloud llegó a retrasar el despertar unos
195 ms y el trabajo unos 99 ms, muy por encima de la reserva finita. No se aumenta
el buffer de la app para ocultar esas pausas artificiales del entorno de pruebas.

Se ejecutaron 6/6 CTest en Linux con GUI y 5/5 pruebas del motor con ASan, UBSan y
LeakSanitizer (sin supresiones para estas pruebas del motor). ThreadSanitizer
aprobó `playback` y `audio-buffering`, incluyendo accesos concurrentes al FIFO,
telemetría, comandos y cierre. En el entorno cloud LSan requiere ejecutar fuera
del monitor ptrace; los resultados válidos se obtuvieron así. Los logs de pruebas
normales y sanitizadas de CI se publican como artefactos `audio-regression-OFF`
y `audio-regression-ON`.

Para repetir el análisis de carreras (dependencias de escritorio instaladas):

```bash
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DAGBPLAY_BUILD_GUI=OFF -DAGBPLAY_BUILD_CLI=OFF -DAGBPLAY_BUILD_NC=OFF \
  -DBUILD_TESTING=ON -DAGBPLAY_ENABLE_AVX2=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan --target test-playback test-audio-buffering
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan \
  -R '^(playback|audio-buffering)$' --output-on-failure
```

Con PCM sintético del motor original, el transporte en callbacks de 96 frames
se compara bit a bit frente a la referencia directa para SINC, BLEP y BLAMP:
diferencia máxima de muestra **0**, mismos RMS y picos. Esto verifica el FIFO,
no una igualdad de salida acústica de Android con escritorio. El tono seno usado
para probar el medidor tiene RMS conocido y el tap conserva incluso ceros con
signo, subnormales y patrones NaN. No se introduce una prueba THD que confunda las
armónicas propias de la muestra triangular con un defecto del backend.

No hay teléfono Android conectado en el entorno de desarrollo. Cero underruns
en simulación o Linux **no** demuestra cero underruns en ese teléfono. Tras
instalar 1.0.4, dejar sonar 60–120 s sin tocar y recoger `Android audio`, `Android
timing` y `Android PCM` en Log. Comparar con la misma canción durante interacción,
con mismo perfil/frecuencia/velocidad/volumen. Si los underruns bajan a cero pero
persiste distorsión, comparar por separado un WAV float exportado a igual perfil
con escritorio y una captura de salida física/loopback. No atribuir a armónicos
ni a volumen un problema que los registros no demuestran.
