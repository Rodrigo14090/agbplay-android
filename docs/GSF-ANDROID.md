# GSFLIB y MINIGSF

## Causa reproducida

El escáner oficial permite entradas de reproductor completamente vacías
(`MP2KScanner::IsValidPlayerTableEntry`). Los rips GSF pueden eliminar datos no
utilizados; además el escáner admite relleno cero al final de la tabla. Cada
entrada mantiene su índice: `ReadU8(playerPos + 8)` se convierte en
`PlayerInfo.maxTracks`, pasa por `playerTableScanned`, `ApplyScanToPlayback()` y
`playerTablePlayback` hasta el constructor de `MP2KContext`.

La comprobación añadida por el port rechazaba **todas** las entradas con cero
pistas, antes de reproducir ninguna canción. Una entrada inactiva junto a otra
válida bastaba para provocar `Invalid player track count: 0`, con o sin playlist.
No se requiere SAF, una ROM corrupta ni una importación MINIGSF para reproducirlo.

El fixture legal `SyntheticGsf.hpp` contiene un reproductor vacío de índice 0 y
otro de 16 pistas de índice 1. Sus cuatro canciones apuntan al segundo. El motor
oficial sin modificar, revisión `0b87da48d2502da359e45718eec8566ac40fa9d7`, lo
escanea y genera PCM: 32 slots detectados (incluido relleno), `0/16` pistas en
los dos primeros slots, energía izquierda acumulada `167.574` en 100 microbloques.
Es una ejecución del motor oficial, no una prueba de su interfaz Qt completa ni
del conjunto concreto del usuario, que no está disponible en este entorno.

La corrección conserva el cero y los índices originales: no inventa capacidades.
Rechaza tablas sin ningún reproductor activo, capacidades mayores de 16 y
canciones pobladas que apunten a un reproductor de capacidad cero. El inicio
directo de una canción en un slot inactivo también se protege. Los slots vacíos
no asignan pistas. El escáner deja de leer al terminar la ROM. También se corrige
la rama de configuración manual que no publicaba los resultados de la tabla y
del modo sonoro, y el guardado de `playerTable` en JSON.
Los perfiles completamente manuales se validan por sus rangos y no necesitan
cumplir el mínimo de cuatro canciones del autodetector. Se conserva la prioridad
al guardar/cargar y se rechazan números inválidos antes de convertirlos a uint8;
un perfil JSON inválido se conserva en disco, se omite y se registra el motivo.

El usuario confirmó que su mismo GSFLIB/MINIGSF funciona en agbplay oficial de
escritorio y que la entrega anterior eliminó los underruns de sus ROMs GBA.
La comparación ejecutada aquí usa datos sintéticos; falta verificar el conjunto
real con esta corrección.

## Importación Android

### Abrir una carpeta de OST (1.0.8)

Usa **Menú > Abrir carpeta GSF…** (o **File > Abrir carpeta GSF…** en
escritorio), entra en la carpeta de un juego y pulsa **Usar esta carpeta** en
el selector Android. La aplicación detecta el GSFLIB, carga el motor e importa
sus MINIGSF sin seleccionar archivos individuales ni responder al diálogo de
playlist. En la vista móvil se muestra la pestaña Playlist con sus títulos;
la reproducción comienza con los controles habituales.

Se leen los archivos directamente contenidos en esa carpeta. No se recorren
subcarpetas de una colección de juegos: selecciona la carpeta del OST individual.
Se ignoran portadas y otros tipos de archivo. Si hay varias bibliotecas, sólo
entonces se pide elegir una; se importan sus selectores asociados mediante `_lib`,
sin mezclar las canciones de las demás. Una biblioteca sin MINIGSF se carga sin
preguntar por la playlist. El conjunto MP2K y los perfiles deben seguir siendo
compatibles; los diálogos existentes de perfiles/código desconocido se mantienen
cuando hagan falta.

La selección nativa usa `ACTION_OPEN_DOCUMENT_TREE` y únicamente su permiso SAF
de lectura. Los IDs del proveedor son opacos y se enumeran con `DocumentsContract`,
sin convertirlos a rutas públicas ni añadir permisos generales. Qt conserva el
permiso persistente cuando el proveedor lo admite. Android 11+ impide seleccionar
la raíz del almacenamiento o toda Descargas; usa una subcarpeta del juego.

La enumeración, copia y lectura de etiquetas se ejecutan en el trabajador de
importación. Se conserva el límite de 64 MiB por documento y se limitan las
consultas a 100000 entradas/65536 archivos GSF. Cancelar evita publicar resultados
y se comprueba entre documentos; una consulta/lectura bloqueada del proveedor no
se aborta a la fuerza. Se validan todos los selectores antes de cerrar el juego
actual; una playlist malformada no lo descarta. El rango de IDs se verifica además
contra el perfil final antes de inicializar la reproducción. La playlist de la
carpeta sustituye la lista en memoria sin otro diálogo y queda disponible para
guardar en el perfil. No se cambia el backend de audio ni los remuestreadores.
Los nombres obtenidos de la enumeración se reutilizan al copiar, sin hacer otra
consulta de metadatos por canción; así se conservan incluso si el proveedor permite
enumerar pero falla al consultar un hijo. En esta modalidad `_lib` debe resolver
a la biblioteca en la propia carpeta, no a una biblioteca homónima en otro nivel.

### Archivos y ZIP

En **Abrir ROM/GSF** se puede seleccionar un GSFLIB y sus MINIGSF simultáneamente
usando la selección múltiple del selector Android. También se puede abrir sólo
el GSFLIB: **No** inicia el motor sin playlist; **Yes** abre un selector para
elegir los MINIGSF asociados o un ZIP. El menú de perfil permite importarlos
posteriormente. No se necesita seleccionar una carpeta ni conceder acceso
general al almacenamiento: cada documento seleccionado recibe su permiso SAF.

Cada URI se copia a su directorio privado de caché conservando el nombre original
seguro. Dos documentos homónimos no se sobrescriben porque sus directorios se
identifican por la URI. Los nombres permiten mantener el orden y comprobar `_lib`;
ya no se intenta descubrir vecinos del almacenamiento público enumerando caché.
Las copias y el análisis de MINIGSF se ejecutan en un trabajador cancelable entre
archivos; el callback de audio y la síntesis no participan. El resultado se aplica
en el hilo Qt sólo si sigue activo el mismo perfil. Un error no publica una
playlist parcial. El lector de un proveedor puede tardar en responder; cancelar
evita aplicar resultados y se comprueba entre documentos, sin abortar a la fuerza
una lectura JNI en curso.

Se admite el selector MP2K de uno o dos bytes, títulos UTF-8/CRLF y asociación
`_lib` con la biblioteca cargada. Los ZIP conservan las rutas relativas internas;
se comprueban asociaciones e identificadores fuera de la tabla. Cargar un ZIP
con el conjunto completo sigue siendo una alternativa. En una ROM GBA la
importación manual verifica los IDs, pero no puede demostrar identidad con una
biblioteca GSF cuyo nombre no forma parte de la ROM.

## Reconstrucción y límites

El lector verifica CRC, longitudes y el límite de 32 MiB. La zona reservada PSF
se trata como datos sin comprimir y se respeta la dirección de carga de cartucho,
incluidos offsets no nulos; las cargas en RAM se rechazan explícitamente. Estas
correcciones son independientes del error de slots vacíos reproducido con carga
normal a `0x08000000`.

agbplay interpreta MP2K/M4A; no emula una CPU GBA para ejecutar drivers ajenos.
Un MINIGSF que no sea un selector MP2K de uno/dos bytes o que requiera varias
bibliotecas se rechaza con un mensaje específico. No se afirma compatibilidad
con cualquier variante GSF. Los conjuntos que dependan de otros GSFLIB o drivers
necesitan investigación adicional; no se inventan parámetros para inicializarlos.

## Pruebas reproducibles

`ctest` incluye `gsf`: reconstrucción, escáner automático, perfil manual,
reproducción PCM con y sin playlist, slots cero válidos/inválidos, orden/títulos,
asociaciones, límites de IDs, ZIP y rechazo de CRC/cargas incompatibles.
`touch-ui` usa un backend de prueba para ejercitar el PlaybackEngine real con
las respuestas Yes/No, importación asincrónica, errores/cierre/reapertura y un
proveedor lento simulado con entrega continua de temporizadores Qt.

La instrumentación SAF Android comprueba nombres preservados, Unicode,
documentos homónimos y rutas inseguras, además de pipes, Descargas y errores.
También prueba enumeración de un árbol SAF con IDs opacos, copia de sus hijos,
deduplicación, filtrado de portadas/directorios, límites y permisos denegados.
La suite Qt abre una carpeta desde cero, importa sin diálogo, cambia de canción,
conserva la reproducción ante selectores dañados/carpetas vacías y prueba elección
y cancelación con varias bibliotecas. El probe Android verifica con toques ADB
que la nueva acción del menú es accesible en los cuatro tamaños existentes.
Las pruebas no necesitan ni publican ROMs comerciales.

Para comparar con el motor oficial en un host x86 con las dependencias de
escritorio instaladas:

```sh
./scripts/test-gsf-reference.sh
```

El script usa el historial oficial ya incluido en Git y un worktree separado;
no modifica las fuentes oficiales. Las comprobaciones físicas con el conjunto
del usuario y la ruta Android ARM64 real siguen pendientes. La política PCM,
el backend QAudioSink y los remuestreadores no se modifican en esta entrega.
