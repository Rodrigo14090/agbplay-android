# MilkDrop / projectM experimental (1.2.0)

Base conservada: `android/audio-compatibility` en
`8fc1d934a2c2c0bb44fdf3d6428c1b386c9ddefe`. Todo el trabajo está en
`android/projectm-visualizer`; no fusionar sin la validación física del usuario.

## Versión y ruta gráfica

Se fija libprojectM **4.1.8**, commit
`4967dc003e5206d7df3ff447287f7609c47d2343`, y su submódulo projectm-eval
`e8c311e86f4d8c082e96e02b6a33525034eee6d4`. API C estable, GLES 3.0 en Android,
GL 3.3 Core en escritorio opcional. No se utiliza SDL, QML, Kotlin, micrófono
ni captura de audio del sistema. No hay cambios en los fuentes de projectM.

`ProjectMRenderer`, en `src/visualization`, sólo conoce la API C, PCM y el contexto
OpenGL actual. `MilkDropWindow` adapta esa clase a QOpenGLWindow. El código de
projectM 4.1.8 (`ProjectM.cpp::RenderFrame`) enlaza el framebuffer **0**; por eso
se usa una ventana nativa embebida con QWidget::createWindowContainer, evitando
el FBO intermedio de QOpenGLWidget y una copia/escalado adicional. El resto de la
aplicación sigue en Qt Widgets. La pestaña independiente conserva Pistas/ROM/Log
y los visualizadores de notas/instrumentos, también en las distribuciones amplias.

En Android se consulta el tamaño real de la superficie EGL actual y se contrasta
con tamaño lógico × devicePixelRatio; se registra también el viewport leído de
GL. No se reduce resolución ni se añade MSAA/profundidad/stencil innecesarios.
Se solicita swap interval 1; frameSwapped y un único timer
preciso marcan los próximos fotogramas, sin ráfagas para recuperar retrasos.
Objetivo inicial 60 FPS. Cada dos segundos, el modo automático puede bajar a
30/20/15/10/5/2/1 si el rendimiento o tiempo de envío GL no sostiene el objetivo.
Cada envío deja además un descanso al menos igual al tiempo de trabajo GL;
incluso un preset inferior al mínimo de FPS no genera una ráfaga continua de
render que sature el hilo. Ese presupuesto se adapta por fotograma. Nunca
cambia resolución, síntesis, prioridades de audio ni parámetros del FIFO. El
mesh de ecuaciones visuales se fija en 32×24; no es el remuestreador de audio.

La pantalla completa es una ventana Qt de presentación: se mueve únicamente el
contenedor GL, conservando pestaña, selección y geometría de la interfaz original.
La ventana original se oculta durante la presentación para evitar mantener un
segundo backing store Qt pintando detrás del visualizador.
En Android se guarda requestedOrientation y se solicita LANDSCAPE (no sensor).
Salir/Back/Escape devuelve el contenedor y restaura el valor anterior, incluso
UNSPECIFIED. Los controles de presets y salida quedan fuera de la superficie GL.

## PCM y concurrencia

El único cambio al camino de reproducción es una copia observacional después
de la ganancia en PlaybackEngine::ConsumeAudio, común a Qt/AAudio y AudioTrack.
Los backends, FIFO, reserva de audio, productor, MP2K y SINC/BLEP/BLAMP no cambian.
El exportador tampoco cambia. El tap comienza deshabilitado y sólo se habilita
mientras la superficie está expuesta y la aplicación activa.

`VisualizationPcmTap` tiene tres bloques de 512 frames estéreo (12 KiB más
metadatos). Un intercambio atómico acquire/release transfiere su propiedad entre
un escritor de audio y un lector gráfico. No hay mutex, espera, asignación,
callback Qt ni llamadas a projectM en el escritor. Las ráfagas pequeñas se
ensamblan sin romper LRLR; los bloques visuales no leídos pueden reemplazarse.
Nunca se devuelve presión al FIFO de reproducción. Un epoch descarta bloques
parciales/antiguos al ocultar o volver a habilitar la visualización.
Los descartes visuales son normales incluso a 60 FPS: a 48 kHz se publican
93.75 bloques/s de 512 frames. No son underruns ni frames perdidos del audio.

El renderizado y todas las llamadas de projectM pertenecen al hilo/contexto GL.
Sin PCM nuevo se entrega silencio, sin repetir el último bloque. projectM no
ofrece parámetro de frecuencia PCM en esta API: recibe muestras float originales,
sin remuestrear el audio para adaptar su análisis visual. No se promete una
sincronización exacta con la latencia física de altavoces/Bluetooth.

Home, pantalla apagada o pestaña oculta detienen la cadena de render y deshabilitan
el tap. La música y el servicio multimedia siguen sus reglas anteriores. El
callback aboutToBeDestroyed libera projectM con el contexto actual; initializeGL
lo crea de nuevo y reaplica el preset. No se conserva un handle GL obsoleto.
La inicialización se aplaza hasta un fotograma expuesto, interceptando tanto
Paint como UpdateRequest de Qt: beginPaint de QOpenGLWindow no protege el acceso
a su tabla GL si falla la creación del contexto. Una inicialización fallida se
rechaza antes de ese camino; el reproductor y su bucle de control siguen vivos.
El cierre libera los objetos con una superficie offscreen del mismo contexto,
sin esperar a que vuelva una EGLSurface de Android ya retirada. La presentación
se cierra antes del quit, acepta Back/cierre y también libera deletes diferidos
si el bucle Qt ya terminó.

## Presets y licencias

Tres presets originales pequeños —Aurora, Orbitas y Espectro—, sin texturas ni
paquetes de origen/licencia dudosos. Sintaxis MilkDrop, licencia MIT incluida.
‹/› y el selector cambian de preset. El botón + importa un .milk mediante el
selector de documentos Qt/SAF, copia hasta 256 KiB en datos privados y persiste
la colección; no requiere permisos generales. La selección se guarda con QSettings.
Los presets que requieran texturas externas no las importan automáticamente.

projectM es LGPL-2.1-or-later; el evaluador y HLSL parser son MIT; GLM se usa bajo
su opción MIT; SOIL2/stb incluyen dominio público y etc1_utils es Apache-2.0.
Los avisos completos están en `licenses/projectM`, dentro del APK y en
Help → Licencias de MilkDrop / projectM. Consulte NOTICE.txt para atribuciones.

La biblioteca se enlaza estáticamente para un despliegue Android simple. Se
publican todos los fuentes de agbplay y scripts; FetchContent fija el fuente
completo de projectM y su evaluador. Para reconstruir/reenlazar con una versión
modificada, sustituir `FETCHCONTENT_SOURCE_DIR_AGBPLAY_PROJECTM` por su árbol
local con submódulos y reconstruir el APK con los scripts existentes. Puede
firmarse con una clave propia. No hay comprobación que impida usar otra biblioteca.

## Construcción y validación

Android: scripts/build-android.sh habilita el visualizador de forma predeterminada.
Requiere GLES 3.0; si falla la creación gráfica, se informa y el reproductor queda
disponible. Escritorio conserva el comportamiento anterior por defecto; activar
con `-DAGBPLAY_PROJECTM=ON` y Qt OpenGL + cabeceras GL/GLX instaladas. CMake descarga
la dependencia fijada; no se suben binarios/dependencias descargadas a Git.

Pruebas añadidas:

- Tap independiente: ráfagas de 96 frames, productor más rápido que el lector,
  latest-wins, bloques parciales, epochs y un millón de frames concurrentes LR.
- Motor real con salida de pruebas por callback y push: 512 frames estéreo del
  tap idénticos muestra por muestra al PCM entregado, incluida la rampa de volumen.
- Linux/Xvfb/GL: tres presets con imagen no negra, framebuffer nativo, pantalla
  completa/salida, pausa del render al ocultar y reanudación.
- Android: probe real con GBA y GSF, ambos backends, teléfono/tablet, orientación
  solicitada y efectiva, restauración, viewport GL/DPR, fotogramas y continuidad
  PCM, Home y pantalla apagada. Se conservan las suites anteriores.
- Contexto no disponible: selección/interfaz y temporizador siguen funcionando
  sin intentar pintar mediante una tabla GL inválida. Retirada de tarea desde
  pantalla completa con perfil pendiente: salida EXIT_SELF/0 y relanzamiento.

`scripts/test-android-milkdrop.py` conserva capturas, JSON, meminfo, CPU ticks
del proceso y logs por escenario en los artefactos de CI. FPS mide presentación;
CPU submit es tiempo de pared del envío GL e incluye posibles esperas del driver,
no tiempo GPU. No se presentan contadores HWUI como tiempo GPU de projectM.
Los resultados y límites físicos pendientes se registrarán junto al APK exacto.

La prueba definitiva de 60 FPS, coste GPU/energía, memoria prolongada y ausencia
de pérdidas con música real requiere el teléfono/MatePad. Un emulador o llvmpipe
no acredita esos resultados. Comparar los mismos temas con MilkDrop visible y
oculto, ambos backends, pantalla completa, suspensión y retorno. Los logs de
audio existentes deben distinguirse de los descartes exclusivamente visuales.
