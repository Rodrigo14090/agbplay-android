# Desplazamiento de Canciones y Playlist en Android

La configuración está localizada en `SonglistWidget::EnableTouchInterface()`;
en producción sólo se activa con la interfaz Android. El escritorio conserva
su vista y configuración originales. No se modifica ningún archivo de síntesis,
FIFO ni salida de audio.

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
