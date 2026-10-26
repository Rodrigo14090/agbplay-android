# Desplazamiento de Canciones y Playlist en Android

La configuración está localizada en `SonglistWidget::EnableTouchInterface()`;
en producción sólo se activa con la interfaz Android. El escritorio conserva
su vista y configuración originales. No se modifica ningún archivo de síntesis,
FIFO ni salida de audio.

## APK de prueba 1.1.2

[APK ARM64 firmado, comprimido en ZIP](https://github.com/Rodrigo14090/agbplay-android/actions/runs/40360711708/artifacts/12668660749).

- Fuente exacta: `eecca14c3c9015dc36f2648acf1323fa512591db`.
- Rama: `android/audio-compatibility`, [PR #2](https://github.com/Rodrigo14090/agbplay-android/pull/2).
- Versión `1.1.2-audio-compat-dev`, versionCode **13**.
- SHA-256 APK: `198667829a388f7f4cb8a4b372239f804dee795623ee2cfe8fafdf338979d1d9`.
- Firma de desarrollo v2 y 17 bibliotecas AArch64 con alineación de 16 KiB
  verificadas en CI. Qt 6.11.2, NDK 27.2.12479018.

Linux local (Qt 6.8.2): compilación y **8/8 CTest** aprobados; las comprobaciones
de arrastre, inercia, rueda y edición también pasan con escala 2× y 3×. CI Linux
(Qt 6.4.2) aprobó **8/8** y las dos repeticiones DPI. Comprobación de sintaxis
local con Clang del NDK/ARM64 y compilación completa del APK aprobadas.

El [workflow del PR](https://github.com/Rodrigo14090/agbplay-android/actions/runs/40360724706)
finalizó correctamente: motor normal y sanitizado, interfaz/DPI, SAF y APK.
En el workflow de push también aprobó el
[recorrido Android API 35 x86_64](https://github.com/Rodrigo14090/agbplay-android/actions/runs/40360711708/job/121705055401):
cuatro escenarios de toques ADB sobre Abrir/Menú/Carpeta y ambos recorridos de
audio, con Home, pantalla apagada, foco, pausa/Stop, modos, pistas, WAV y GSF.
[Capturas y registros](https://github.com/Rodrigo14090/agbplay-android/actions/runs/40360711708/artifacts/12669930027).
Los gestos de desplazamiento se verificaron mediante QScroller en Linux; esos
toques ADB existentes no evalúan la sensación de arrastre en un dispositivo.
Los dos trabajos Linux duplicados del push seguían instalando dependencias al
cerrar este registro; sus equivalentes del PR ya habían aprobado. No se declara
finalizado todo el workflow de push por haber pasado esas pruebas en el PR.

## Causa y corrección

`QScroller` entrega posiciones en píxeles a las barras de `QAbstractScrollArea`.
Las listas conservaban `ScrollPerItem`, cuyas barras cuentan filas. En una
reproducción automatizada anterior a la corrección, un movimiento de **20 píxeles
lógicos desplazó 1008 píxeles de contenido** en ambas listas. Los altos mínimos
táctiles de las filas agravaban esa conversión incorrecta.

Se usa `ScrollPerPixel` en los dos ejes. El arrastre sigue el dedo 1:1 después del
umbral de inicio, sin multiplicar ni dividir coordenadas por el DPR. Qt convierte
las unidades físicas de QScroller según el DPI de la pantalla. No se modifican
los valores globales de QScroller ni los manejadores de eventos de selección,
casillas, rueda o arrastrar y soltar.

Los parámetros locales son: umbral de **3 mm**, suavizado de velocidad **0.4**,
velocidad máxima **0.25 m/s**, desaceleración **0.35** con curva `OutQuad`, sin
aceleración de flicks sucesivos ni rebote en los extremos. Esto conserva la
inercia con una duración máxima aproximada de **0.71 s** para un flick en un eje.
La velocidad máxima limita el impulso al soltar; no reduce el seguimiento del
dedo mientras se arrastra.

`QListView` calcula automáticamente el paso de la rueda a partir de la altura
de fila al usar `ScrollPerPixel`. No se fija un paso en píxeles que pueda quedar
desactualizado al cambiar la fuente o el DPI.

Referencias inspeccionadas: fuentes de Qt **6.11.2**,
`src/widgets/widgets/qabstractscrollarea.cpp` (ScrollPrepare/Scroll),
`src/widgets/itemviews/qlistview.cpp` (actualización de barras) y
`src/widgets/util/qscroller{,properties}.cpp` (DPI y cinemática).

## Verificación

`TestTouchUi` comprueba el desplazamiento real de contenido para un arrastre de
20 píxeles en ambas listas, límite y finalización de la inercia, integridad de
IDs/casillas, equivalencia con la rueda de la lista original, selección, cambios
de casilla y reordenamiento mediante Bajar canción. También conserva InternalMove.
Usa `QScroller::handleInput` y eventos Qt; no sustituye una prueba con el dedo.

El workflow existente ejecuta la suite completa y repite estas pruebas con
`QT_SCALE_FACTOR=2` y `3`, además del factor normal. Compila el APK ARM64 y
conserva las verificaciones Android de interfaz, SAF y reproducción.

Para validar físicamente: probar un arrastre lento de una fila, flick corto,
flicks sucesivos, detener la inercia, llegar a ambos extremos, tocar canción y
casilla, reordenar Playlist y usar la rueda de un ratón conectado. Comparar
teléfono y tablet con fuente normal y ampliada. La sensación táctil final y los
DPI comunicados por cada dispositivo requieren esa comprobación física.
