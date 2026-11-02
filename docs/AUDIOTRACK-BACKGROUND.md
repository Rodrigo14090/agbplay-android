# AudioTrack: microcortes en segundo plano (1.1.1)

## Evidencia y límites

El usuario confirmó sonido limpio mediante AudioTrack en primer plano en su
MatePad y teléfono; en la tablet percibe microcortes al ocultar la app/apagar
pantalla, con optimización de batería ya desactivada. No se cambian las opciones
de procesamiento que le funcionan ni se atribuye el problema a ese ajuste.

El fragmento facilitado contiene 40 intervalos, unos **204.33 s**:

- **38** lecturas con déficit de PCM; **5480 frames** solicitados no disponibles,
  equivalentes a **114.17 ms** a 48 kHz. Un déficit de 40 frames dura 0.833 ms.
- `AudioTrack.underrunCount=0` en todos los informes nativos; sesión y ruta
  permanecen estables. Formato float32 estéreo, 48 kHz, MEDIA/MUSIC, performance 0,
  búfer nativo **1924 frames (40.08 ms)**.
- Reserva del productor **1440/1200 frames (30/25 ms)**; bloques sintetizados
  de **200 frames**, lecturas de **240**, separaciones máximas de lectura de
  unos **22–24 ms** en numerosos intervalos.
- Sin clipping, muestras no finitas ni pérdidas de telemetría en ese fragmento.

No hay marcas inequívocas de transición de la Activity en el fragmento; no se
asignan intervalos concretos a primer plano/pantalla apagada sin confirmación.
La causa de la distinta planificación entre dispositivos (gobernador de CPU,
coalescencia de temporizadores, carga y política del fabricante) no está medida.
Los registros completos del usuario no se publican en el repositorio; sólo este
resumen técnico.

## Defecto identificado

El escritor AudioTrack reutilizaba el callback de salida con deadline fijo.
`AudioFrameQueue::Take` copia lo que existe y rellena con ceros lo restante.
`RenderAudio` entrega siempre el bloque completo; AudioTrack recibe también los
ceros y por ello puede informar **cero underruns nativos** mientras se oyen huecos
ya insertados en su entrada.

El escritor mantenía además un reloj absoluto de 5 ms. Si la espera de escritura
o del planificador lo retrasaba, recuperaba el tiempo mediante varias lecturas
inmediatas. Las ráfagas nativas y las demoras de síntesis podían dejar menos de
240 frames disponibles (por ejemplo 200), aunque todavía hubiera audio en el
búfer nativo para esperar al productor. Era incorrecto imponer al transporte
push la obligación de rellenar de inmediato una solicitud de callback.

Esta ruta de inserción está confirmada por código y por los contadores del
usuario. Es compatible con parte o todos los microcortes descritos; no demuestra
que no pueda existir otra interrupción posterior a la salida de la app.

## Corrección

- AudioTrack sigue usando exactamente la misma clase Java, formato, atributos,
  sesión generada, performance NONE y tamaño solicitado. No se sube volumen ni
  se modifica DSP, prioridades, wake locks o permisos de batería.
- El ritmo depende de la contrapresión de AudioTrack, sin reloj de recuperación
  independiente. Las escrituras permanecen no bloqueantes y conservan offset y
  resto de cada bloque, incluido un bloque parcialmente aceptado al pausar.
- Se añade lectura PCM disponible, **sin relleno**, al FIFO existente. Qt y
  PortAudio conservan su lectura con relleno para callbacks con deadline real.
- Una señal armada antes de leer permite esperar publicaciones del productor sin
  perder notificaciones. El productor conserva su política de reserva y sólo
  añade la publicación de esa señal. La espera ocurre en el escritor, nunca en
  el callback ni en el hilo de interfaz; el cierre la cancela explícitamente.
- Ambos caminos comparten la misma ganancia y medición. Una lectura vacía no
  avanza muestras, volumen ni medición. MP2K/M4A, SINC/BLEP/BLAMP y
  AudioBufferPolicy quedan intactos.
- Se distinguen aplazamientos/lecturas cortas, espera PCM, backpressure nativo y
  frames aceptados de los contadores de pérdida de callbacks y AudioTrack.
  Los watchdogs del transporte conservan el retorno controlado a Qt ante fallos.

Una reserva mayor podría ocultar algunos casos, pero no arreglaría la obligación
incorrecta de fabricar PCM. Esta corrección mantiene los tamaños anteriores.

## Pruebas reproducibles

`test-audio-push` usa señales sintéticas reproducibles y los tamaños del registro:
200/240/400 frames de producción, solicitudes máximas de 240, búfer nativo 1924
y consumo en ráfagas de 960 cada 20 ms. Separa coste de síntesis (con picos de
9.5 ms) y demora de activación (2.2/10/22 ms):

- Modelo anterior: **484 frames rellenados**, con **cero pérdidas nativas**, en
  un caso de 30 s; reproduce el patrón que motivó esta corrección.
- Nuevo consumidor: cero ceros artificiales, cero pérdidas nativas y PCM estéreo
  en orden en los nueve escenarios de 30 s, manteniendo la reserva de 30 ms.
- Una demora deliberadamente excesiva de 60 ms provoca pérdidas nativas en el
  modelo nuevo. Se exige detectarlas: ausencia de relleno no es una certificación
  de continuidad del hardware.
- 400000 frames publicados/consumidos concurrentemente prueban orden, canales y
  señalización, además de cancelación. Se verifican lecturas parciales/vacías y
  descarte de transporte.

`test-playback` prueba el productor real por ambos contratos de lectura. Compara
960 frames estéreo de WAV por callback y por push; en push una lectura vacía deja
intactos PCM y ganancia y la rampa posterior coincide muestra por muestra.
Se conservan las pruebas de ROM, GSF, perfiles, cambio/fallback, controles y PCM.
El probe Android extiende AudioTrack a 30 s oculto y 60 s con pantalla apagada,
además de pausa, foco, modos, WAV, GSF/MINIGSF y cierre.

Los resultados, APK y commit exacto de cada entrega están en
[VALIDATION.md](VALIDATION.md). Los modelos y emuladores no certifican el resultado
acústico de una MatePad. No hay dispositivo físico conectado.

## Verificación en la tablet

Mantén **Compatibilidad multimedia / AudioTrack**, los mismos ajustes musicales
y de volumen y la optimización de batería como ya la tienes. Escucha primero
en primer plano y después 2–3 minutos oculto/con pantalla apagada. Guarda el log
al volver y anota los momentos de transición.

Comprueba por separado:

- `AudioTrack native`: `underrunCount`, avance del playback head, misma ruta y
  formato. Un incremento de ese contador requiere investigar falta de suministro
  real al dispositivo aunque el FIFO ya no inserte ceros.
- `AudioTrack push`: aplazamientos, lecturas cortas y espera máxima pueden ser
  normales si aún existe PCM en el búfer nativo. No son por sí mismos cortes.
- `Android PCM`: continuidad de las muestras, RMS/pico y clipping/no finitos.

Si continúan los microcortes, estos datos permiten localizar la siguiente capa.
No se pide desactivar otra vez la optimización de batería ni se promete que una
prueba sintética haya resuelto la reproducción física.
