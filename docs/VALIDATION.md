# Validación de la entrega

## GSFLIB y MINIGSF (octava entrega)

[Descargar APK 1.0.7 ARM64 firmado (ZIP)](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39789875750/artifacts/12430936917)

- Commit exacto compilado: `cf544e9529d2edbe9bd581b3c021080766fe37ae` (workflow push y artefacto del mismo SHA).
- SHA-256 del APK: `5f8085078b7e6833c9d86bb141ebb21075fbdd3a08c9bda21985b352c4288e8e`.
- `org.agbplay.android`, `1.0.7-android-dev`, versionCode **8**; mínimo API 28, target API 35, compile SDK 36.
- Firma de desarrollo APK v2 verificada, un firmante; 17 bibliotecas AArch64 con LOAD alineados a 16 KiB.
- Qt Multimedia 6.11.2, NDK 27.2.12479018, código nativo Release.
- [Construcción y regresión](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39789875750).
- [Investigación, uso y límites](GSF-ANDROID.md); [instrucciones de construcción](ANDROID.md).

La regresión del port rechazaba cualquier `PlayerInfo.maxTracks == 0`. El
escáner oficial permite slots vacíos en GSF; el contador se leía correctamente
de la tabla y se conservaba al aplicar el perfil. Se reproducía el mismo error
antes de reproducir, tanto con Yes como con No en la importación. Se conservan
los ceros e índices y se validan por separado las capacidades y las canciones
que usan esos slots. No se sustituye el contador por un número arbitrario.

Se ejecutó el motor oficial sin modificar (`0b87da48d2502da359e45718eec8566ac40fa9d7`)
con el mismo GSFLIB sintético legal: detecta 32 slots (incluido relleno), los dos
primeros con capacidades 0/16, y produce PCM no nulo (energía izquierda 167.574
en 100 microbloques). `scripts/test-gsf-reference.sh` reproduce la comparación.
El usuario confirma que su conjunto real funciona en el escritorio oficial;
no se ha ejecutado aquí ese conjunto, que no está disponible en el entorno.

Android permite seleccionar el GSFLIB y varios MINIGSF juntos, o elegir éstos
al responder Yes después de inicializar el motor. No inicia un recorrido de la
carpeta de caché buscando documentos vecinos. La copia SAF conserva nombres en
directorios separados por URI. Copia y análisis de MINIGSF se hacen en segundo
plano con cancelación entre archivos y publicación transaccional para el perfil
vigente. La reconstrucción/escaneo de la ROM principal conserva su ruta Qt
existente; no se afirma que esa fase completa sea asincrónica.

Verificación local del código compilado:

- Escritorio Qt: **7/7 CTest**, incluidas reproducción GBA, paridad de remuestreo,
  trabajador/FIFO y GSFLIB. La prueba GUI ejerce Yes/No, playlist, error y reapertura,
  cancelación y respuesta de temporizadores durante un proveedor lento simulado.
- GSFLIB con ASan/UBSan/LeakSanitizer: **1/1**, sin supresiones.
- GUI con ASan/UBSan/LeakSanitizer: **1/1**, con la única supresión conocida de
  `QScroller::grabGesture` de Qt 6.8.2; no se suprimen errores de agbplay.
- Clang NDK ARM64: comprobación sintáctica del probe Qt con las cabeceras nuevas.

CI comprueba por separado el motor normal y sanitizado, la GUI Linux, SAF con
ContentResolver en emulador Android API 35 y los cuatro casos de botones/toques
Android. El APK ARM64 se construye y su firma/ABI se validan en ese mismo workflow.
Las pruebas SAF son **7/7** y las de copia Java **7/7**. No se confunde el emulador
x86_64 con una reproducción GSFLIB en un teléfono ARM64 físico.

El usuario confirma que la corrección anterior eliminó los underruns observados
con GBA. Esta entrega no cambia `PlaybackEngine`, `AudioFrameQueue`, `QAudioSink`,
la política PCM ni los remuestreadores. Sigue pendiente instalar este APK en el
teléfono y verificar el conjunto real: Yes y No, selección múltiple/ZIP, títulos,
cambios de canción, reproducción continuada y cierre/reapertura. No hay un
dispositivo Android físico conectado al entorno.

## Menús Android y barra nativa (séptima entrega)

[Descargar APK 1.0.6 ARM64 firmado (ZIP)](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39781877957/artifacts/12426209165)

- Commit exacto compilado: `6e96c0630fc89288b914adba14c46b44d16f8081` (checkout push verificado).
- SHA-256 del APK: `53c04c78b039ad5ed34903b893308004332de0ffdf84a006527c41a0aa922756`.
- `org.agbplay.android`, `1.0.6-android-dev`, versionCode **7**; mínimo API 28, target API 35, compile SDK 36.
- Firma APK v2 válida, un firmante; 17 bibliotecas AArch64 con LOAD alineados a 16 KiB.
- [Workflow de la corrección](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39781877957).
- [Capturas Android completas y estado de la corrección](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39781877957/artifacts/12427621867).

El usuario confirmó que **1.0.5 sigue sin mostrar Abrir ROM/GSF ni Menú** en su
teléfono. Se conserva ese resultado; pasar las pruebas Linux no significó que
la desaparición estuviese corregida en Android.

Qt 6.11.2 mantiene la integración nativa de QMenuBar aunque se oculte su QWidget.
QtActivityBase.onPrepareOptionsMenu puede volver a mostrar la ActionBar cuando
hay menús registrados. Esa barra no pertenece al árbol de widgets comprobado por
las pruebas Linux y es una posible superposición de la primera fila.

1.0.6 desactiva el menú nativo antes de construir las acciones y declara un tema
Android DayNight sin ActionBar ni título nativo. Se conserva la apertura SAF,
todos los menús mediante el botón Menú, los paneles y el audio de 1.0.4.
La corrección no cambia los tamaños de filas ni los remuestreadores.

Se añadió una prueba con MainWindow real, Qt Android 6.11.2, emulador API 35,
toques ADB y capturas de la pantalla Android completa. La interfaz de 1.0.5
(commit de prueba `6a09e444a945656c25d807d4f4b1e442a92348cb`) **reproduce el
fallo**: Qt reporta ambos botones visibles y contenidos en sus ancestros, con
centros físicos `(236,77)` y `(518,77)` en una ventana lógica de 384×853, pero
el toque de Android no activa apertura (`openRequests=0`). La prueba falla por
timeout; las pruebas Linux del mismo commit pasan.

- [Workflow anterior que reproduce el fallo Android](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39781338813).
- [Captura Android y estado anterior](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39781338813/artifacts/12425889656).

La corrección **pasa los cuatro casos de entrada Android** sobre el mismo
MainWindow y Qt 6.11.2: teléfono 576×1280 con densidad 240, el mismo tamaño con
fuente 1,3×, horizontal 1280×576 y tablet 1600×1200 con densidad 160. En cada caso
se exige que `getActionBar()` sea nulo, que el menú Qt no sea nativo, que los
botones estén visibles y contenidos y que los toques ADB activen apertura y Menú.
El reporte del probe se captura desde Android con screencap, no QWidget::grab.
Los registros CI contienen cuatro líneas PASS para esas pulsaciones reales.

El workflow completo de la corrección aprobó sus **seis trabajos**: motor normal
5/5, motor ASan/UBSan/LeakSanitizer 5/5, GUI/motor Linux 6/6, copia Java 7/7 y
SAF Android 5/5, interfaz Android en sus cuatro tamaños y APK ARM64 firmado.
Localmente: escritorio compilado y 6/6 CTest; el probe pasó comprobación
sintáctica con Clang NDK ARM64. El código del motor y del backend de audio no
cambia respecto a 1.0.5. La ejecución de la interfaz Android fue en emulador
x86_64; el APK entregado está compilado para ARM64. Sigue pendiente la
comprobación en el teléfono físico del usuario.

## Apertura visible en vista móvil (sexta entrega)

[Descargar APK 1.0.5 ARM64 firmado (ZIP)](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39778757850/artifacts/12425807404)

- Commit exacto compilado: `bc96d005a0f92878d92e574bf7f7c0b35ad51476` (checkout verificado en el workflow push).
- SHA-256 del APK: `9cce377f9e79630093bf01d15802a68faa1a3a73ed4840aa520a2920d73ec6b4`.
- `org.agbplay.android`, `1.0.5-android-dev`, versionCode **6**; mínimo API 28, target API 35, compile SDK 36.
- Firma APK v2 válida, un firmante; 17 bibliotecas AArch64 con LOAD alineados a 16 KiB.
- [Construcción del APK y capturas](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39778757850).
- [Capturas de interfaz en Linux](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39778757850/artifacts/12425816618).
- [Workflow completo del mismo commit en el PR, cinco trabajos aprobados](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39778764891): GUI/motor 6/6, motor normal 5/5, sanitizado 5/5 y SAF en emulador Android 5/5; copia Java 7/7.

La captura del teléfono muestra ausente la fila completa de **Abrir ROM/GSF /
Menú**, mientras transporte, volumen y pestañas siguen presentes. El fallo
exacto no se ha reproducido con el estilo Qt de Linux; no se atribuye una
causa interna confirmada al estilo Android a partir de esa captura.

La corrección mantiene la fila de apertura instalada permanentemente en un
layout de bloques. Cambiar entre una, dos y tres columnas cambia las direcciones
de los layouts, sin eliminar/reinsertar widgets durante resizeEvent. Los botones
de apertura/menú se crean con su padre final y las tres filas se muestran
explícitamente. Los tamaños preferido, mínimo y dependiente del ancho tienen en
cuenta las métricas de las filas, en vez de suponer siempre 48 unidades por fila.
Se mantienen los puntos de ruptura, todos los menús, el selector SAF y el audio
de 1.0.4 sin modificaciones.

Verificado localmente: compilación de escritorio, pruebas del motor 5/5 y suite
de interfaz ampliada. Se comprueban 27 transiciones existentes y 48 pulsaciones
de apertura adicionales partiendo de ventanas 360×800, 384×854, 412×900 y
740×360, con la pestaña ROM, fuentes de 12/20 puntos, una acción de 72 unidades
de alto, ocultación/reaparición y ambos puntos de ruptura. Se verifica el recorte
por cada ancestro y el widget que realmente recibe la pulsación en coordenadas
de ventana. Las capturas son de Qt Widgets en Linux.

La suite de interfaz también pasó ASan/UBSan/LeakSanitizer localmente usando
únicamente la supresión conocida de `QScroller::grabGesture` de Qt 6.8.2
(30 registros, 480 bytes, reproducidos sin agbplay en la investigación anterior).
No se suprimen errores de las clases de layout de agbplay.

La comprobación visual y de apertura en el teléfono Android real sigue pendiente;
no hay un dispositivo físico conectado a este entorno. La captura original y
estas pruebas no demuestran por sí solas la causa exacta ni la resolución en ese
estilo/plataforma; el APK permite verificarlo en el teléfono.

## Reserva temporal de PCM y corrección de underruns (quinta entrega)

[Descargar APK 1.0.4 ARM64 firmado (ZIP)](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39773826126/artifacts/12424300526)

- Commit exacto del APK: `50f5949af673b21ff4a090bf9555a0a5eb3f462a`.
- SHA-256 del APK: `64467adfee7a185e51b9bbfe0bd50ad586322d4e83aeb319475def964a841c29`.
- Firma APK v2 válida, un firmante; 17 bibliotecas AArch64 con LOAD alineados a 16 KiB.
- `org.agbplay.android`, versionName `1.0.4-android-dev`, versionCode **5**; mínimo API 28, target API 35 y compile SDK 36.
- Qt Multimedia 6.11.2, NDK 27.2.12479018, código nativo Release y paquete de desarrollo firmado.
- [Workflow de construcción y regresión](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39773826126).
- Workflow completo **aprobado**: cinco trabajos, incluidos host normal/sanitizado, GUI, SAF Android y APK ARM64.
- [Informe CI normal](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39773826126/artifacts/12424262312).
- [Informe CI sanitizado](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39773826126/artifacts/12424245497) (incluye resultados completos de las pruebas de audio).

El usuario confirmó numerosos underruns físicos de 1.0.3 con callbacks de 96
frames a 48 kHz. La política limitaba la reserva a dos callbacks (192 frames,
4 ms) y dormía 1 ms sin garantizar cuándo volvería a ejecutarse. El motor genera
microbloques de 200 frames y las demoras del productor medidas en el teléfono
llegaban a 10,14 ms. Aumentar la prioridad no cambiaba ese presupuesto insuficiente.

Android ahora mantiene como mínimo 30 ms de PCM, precarga antes de abrir/reanudar
el dispositivo y repone por lotes desde un umbral de 25 ms con plazos monotónicos
calculados. La capacidad física del FIFO sigue siendo 32768 frames; la síntesis
conserva sus microbloques y los remuestreadores originales. El productor no espera
por el mutex de copia del visualizador. El cierre evita notificaciones perdidas;
el dispositivo deja de consumir antes de suspender la síntesis. El callback copia
la captura PCM para analizar RMS/clipping fuera de tiempo real. Se miden objetivos,
pico del productor, frecuencia de callbacks y retraso de despertar por separado.

Comprobado localmente sobre el código final:

| Comprobación | Resultado |
| --- | --- |
| Linux normal con GUI y motor | 6/6 CTest; trabajador sin UI: 1423 callbacks, 0 underruns / missing frames, mínimo 1048 frames, pico 1632 |
| Motor ASan/UBSan/LeakSanitizer | 5/5, sin supresiones; repetición final de audio 2/2, sin errores de memoria |
| ThreadSanitizer de trabajador/FIFO | 2/2, sin carreras; ejecución instrumentada fuera del presupuesto temporal, no certifica continuidad |
| Simulación: 96 frames / 48 kHz, bloques 64–1024, tardanza 10 ms y picos de trabajo 5 ms | 0 underruns con reserva 30 ms para cada tamaño |
| Simulación de 600 s con tardanza 15 ms | 300000 callbacks, 0 underruns |
| Ensayo de reservas de 20 y 40 ms con bloques 200 y tardanza 10 ms | 20 ms: 3 underruns / 120 frames ausentes; 40 ms: 0 |
| Sobrecarga de 45 ms o síntesis permanente de 200 frames cada 5 ms | Pérdidas detectadas/contadas; conservación del orden de PCM y recuperación |
| PCM directo frente a FIFO/callback, SINC/BLEP/BLAMP | Diferencia máxima de muestra 0, bit a bit; RMS y pico idénticos por resampler |

Los datos de transportes PCM sintéticos fueron: SINC RMS `0.0375162`, pico
`0.371804`; BLEP RMS `0.0374493`, pico `0.362582`; BLAMP RMS `0.0373691`, pico
`0.332435`. No son una comparación ARM64 contra x86 de la ROM del usuario ni
una medición acústica. El medidor se verifica además con un seno de RMS conocido,
ceros con signo, subnormales, NaN/Inf y floats finitos extremos.

Las pruebas con hilos reales no tienen un planificador de tiempo real garantizado:
una repetición cloud superó 195 ms de retraso. La simulación exige cero pérdidas
con retrasos acotados; el trabajador exige continuidad si sus demoras observadas
caben en el presupuesto y siempre verifica datos/ciclo de vida. La última prueba
ThreadSanitizer registró 118 underruns / 9664 frames ausentes mientras la síntesis
máxima llegó a 13,82 ms (para microbloques de 4,17 ms de audio). Estos máximos son
tiempo transcurrido e incluyen posibles preempciones, no miden coste CPU medio.
Pasar ThreadSanitizer
significa que no detectó carreras en esos caminos, no que el sonido instrumentado
fuera estable en tiempo real. La última prueba normal y la ASan dieron 0 pérdidas.

El motor `src/agbplay` no cambia frente a 1.0.3. Las modificaciones están en el
transporte/planificación de `agbplay-util`, telemetría, pruebas, versión y
construcción/documentación. No se cambian perfiles, remuestreadores ni ganancia.

No hay teléfono conectado en este entorno. **El criterio de cero underruns en
Android real permanece pendiente.** Probar 60–120 s sin tocar con la misma ROM,
perfil y configuración; recoger las líneas `Android audio`, `Android timing`
y `Android PCM`. Si los contadores de cola quedan a cero pero persiste distorsión,
comprobar aparte la salida float exportada y la salida física hasta AAudio.
Ver [análisis completo, política y reproducción de pruebas](AUDIO-ANDROID.md).

## Prioridad del productor, telemetría y APK ARM64 (cuarta entrega)

[Descargar APK 1.0.3 ARM64 firmado (ZIP)](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39726795680/artifacts/12403139872)

- [Workflow completo aprobado](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39726795680): ARM64, host normal/sanitizado, interfaz y pruebas SAF Android.
- SHA-256 del APK: `eaddec7a4b746f5af4cf53018ec9091698c2c12b68d76a62f7066ff60a91dca0`.
- Firma APK v2 válida, un firmante; 17 bibliotecas AArch64 con LOAD alineados a 16 KiB.
- `org.agbplay.android`, versionCode **4**, mínimo API 28, target API 35, compile SDK 36, Qt 6.11.2 y NDK 27.2.12479018.

Commits de implementación y publicación: `95361daa26580c6adeba1441cc027845849d43f9` y `6544579f3015cf30d7efea4d8e8b5c9aacdeae24`.

El callback Qt 6.11 usa AAudio y no depende del event loop. La síntesis ya corría
en su propio hilo; ahora ese productor solicita `THREAD_PRIORITY_AUDIO` para que
la planificación de Android no dependa de actividad táctil. El backend no
fuerza tamaño con `setBufferSize()` en la ruta callback, porque Qt documenta que
el tamaño lo elige el backend. El formato solicitado sigue siendo float32,
estéreo, con la frecuencia configurada; la ruta de síntesis y sus
remuestreadores no cambiaron. En escritorio las mediciones costosas no se
ejecutan y PortAudio conserva su procesamiento de muestras.

La ventana **Log** registra cada cinco segundos underruns y frames rellenados
con silencio, ocupación mínima/máxima de la cola, tamaños e intervalos de
callback, duración máxima de síntesis/callback, RMS/pico posterior al control de
volumen, muestras recortadas/no finitas y los valores de formato/buffer que Qt
reporta. Así se puede distinguir continuidad de amplitud/clipping. La prueba de
escritorio con ROM sintética, 48 kHz y 600 bloques registró RMS `0.0373691`,
pico `0.332435` en `240000` valores de canal. La cola mantuvo orden de muestras y
contó correctamente un callback con cuatro frames ausentes en la prueba
forzada.

Verificado localmente con la suite de escritorio: **5/5 CTest**. CI aprobó los
cinco trabajos, incluida compilación ARM64, firma/ABI/alineación, pruebas
normales y sanitizadas del motor y emulador API 35 para proveedores SAF.
`PlaybackEngine.cpp` también pasó comprobación sintáctica con Clang NDK ARM64.
No se dispone de un Android físico en este entorno, así que los contadores de la
canción del usuario y la escucha posterior a la corrección siguen pendientes
de prueba en su teléfono. La ruta de audio Qt/AAudio se compiló y empaquetó, no se
ejecutó en ese teléfono.

## Corrección de lectura SAF (tercera entrega)

[Descargar APK 1.0.2 ARM64 firmado (ZIP)](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39723428465/artifacts/12401649639)

- [Workflow completo aprobado](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39723428465): cinco trabajos, incluyendo pruebas de proveedores Android en emulador.
- SHA-256 del APK: `f51b37df24533ace39ae74398b63f24a05a869b2eb045950e1cd7cac6b330722`.
- Firma v2 válida; 17 bibliotecas AArch64 con LOAD alineados a 16 KiB.
- [Resultado de instrumentación Android: OK (5 tests)](https://github.com/Rodrigo14090/agbplay-android/actions/runs/39723428465/artifacts/12401009957).

Código: `2c93eef2fa64db50b52238d7c9a562a955928bc0`; APK `1.0.2-android-dev`,
versionCode **3**. El usuario reportó «Cannot read selected document: Unknown
error» al abrir una GBA desde almacenamiento interno/Descargas. Ese mensaje
procedía del fallo de QFile::open(content://), antes del análisis de la ROM.
No se confirmó una causa concreta dentro del proveedor del teléfono.

La importación ahora utiliza directamente ContentResolver.openInputStream de
Android. Qt mantiene el selector SAF, pero ya no interpreta ni reconstruye la
URI para leerla con su motor de archivos. selectedUrls conserva la codificación,
DISPLAY_NAME aporta la extensión original cuando está disponible y los metadatos
son opcionales. La copia secuencial de 64 KiB no requiere tamaño/seek/available,
se limita a 64 MiB y se publica de forma atómica. Se conserva la copia previa y
el juego abierto si falla. JNI conserva los detalles de la excepción Android.
También se controlan los errores de lectura al importar playlists MINIGSF.

Comprobado:

- Compilación local de DocumentAccess con NDK 27.2/Clang/AArch64/API 28.
- GUI de escritorio compilada y 5/5 pruebas CTest aprobadas localmente y en CI.
- 4/4 pruebas del motor normales y con ASan/UBSan/LeakSanitizer aprobadas en CI.
- 7 pruebas Java de copia: comparación byte por byte con lecturas cortas, límite
  exacto y exceso de 64 MiB, stream vacío, errores de lectura/escritura y lecturas
  sin progreso. No se usa tamaño ni available().
- 5 pruebas de instrumentación aprobadas en emulador Android API 35 x86_64,
  ejecutando el mismo DocumentImporter que se incluye en el APK ARM64:
  proveedor real MediaStore Downloads del almacenamiento interno; proveedor
  sintético con pipe, tamaño desconocido, nombre Unicode/#/% y URI con ID
  codificado; metadatos denegados con lectura válida; fallo de lectura conservando
  la copia anterior y eliminando temporales; permisos denegados con detalle.

Las pruebas Android usan datos generados, no ROMs comerciales. La entrada creada
en Descargas se elimina al terminar. Es una app separada org.agbplay.saftest;
no sustituye ni requiere modificar la instalación de agbplay.

No se ha repetido todavía la apertura de la ROM del usuario en su teléfono.
La instrumentación verifica el importador Java y proveedores Android; la
compilación del APK verifica el puente JNI, pero su ejecución desde la interfaz
Qt en ese teléfono, la reproducción y otros proveedores siguen requiriendo
pruebas físicas. No se modificaron el motor C++ ni el backend de audio.

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
