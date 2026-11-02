# Reanudación, menús y eliminación desde recientes (1.1.4)

## Referencia y alcance

Base anterior conservada: `a66eda9cb3c09b3361108f2a6d4a68e4f72fa3a4` (1.1.3).
Trabajo en `android/audio-compatibility`, sin modificar `android/arm64-native`
ni `main`. Se conserva el scroll por filas al 50 %, el sintetizador, los
remuestreadores y ambos backends de audio con su reserva PCM existente.

El usuario reportó un cierre persistente al abrir menús/SAF después de escuchar,
pausar y volver de pantalla apagada. También reportó audio que continúa al quitar
la tarea y una ventana gris al relanzar. No se dispone de su stack trace ni de
la tablet física: no se afirma una causa confirmada del cierre persistente ni
que borrar datos/reinstalar sea una solución al defecto.

## Defectos del código abordados

- El cierre normal de MainWindow pregunta por perfiles pendientes **antes de
  detener el audio**. QApplication::quit() pasa por ese cierre. El teardown de
  QtActivityBase de Qt 6.11.2 puede esperar a que vuelva el main() nativo; una
  pregunta oculta no puede recibir respuesta y puede impedir esa terminación.
- `stopWithTask=true` confía la parada al sistema y evita onTaskRemoved(). No
  hay una petición explícita e independiente de los comandos Play/Stop que
  termine la aplicación Qt al desaparecer su Activity/tarea.
- Se modificaban los flags de repintado de **todas** las ventanas, incluidos
  popups y diálogos, en cada cambio de ApplicationActive. Eso también altera
  bloqueos intencionados de widgets y añade una política paralela a las
  superficies que ya gestiona Qt.
- El filtro táctil redimensionaba los diálogos durante Show, después de que Qt
  los hubiera centrado con su tamaño anterior, sin recolocar el origen. También
  envolvía Preferencias por segunda vez. En la prueba de tablet 1600×1200,
  Cancelar quedó en (1359,1389), fuera de pantalla. Preferencias conserva ahora
  su scroll propio y los demás formularios se colocan en la geometría disponible
  completa; las pruebas rechazan objetivos táctiles fuera de la pantalla.
- Los callbacks de un servicio ya destruido podían entrar en métodos que
  utilizan una MediaSession/foco liberados. El vencimiento de la pausa mantiene
  el motor C++ pausado; no garantiza que todos los callbacks Java antiguos se
  hayan consumido al liberar el servicio.
- Un error de escritura de ajustes al perder foco podía salir del slot como
  una excepción C++ no capturada. Se informa del fallo y se conserva dirty para
  reintentar, sin lanzar un diálogo sobre una Activity oculta.

Estas rutas se identifican en el código; no equivalen a una traza del incidente
concreto del usuario. En particular, que sólo una reinstalación lo recupere
requiere registros del dispositivo para distinguir crash Java, native crash,
ANR, estado del proveedor de documentos o un fallo del sistema gráfico.

También se comprobó en `QAndroidPlatformFileDialogHelper::setInitialDirectoryUri`
de Qt 6.11.2 que EXTRA_INITIAL_URI se envía como String. El contrato Android pide
un Parcelable Uri. AgbplayActivity normaliza únicamente ese hint opcional: convierte
content:// a Uri y elimina rutas privadas file:// que el proveedor no puede
abrir. Se conserva el selector Qt, sus tipos MIME, selección múltiple y grants;
no se reimplementa SAF ni se cambian permisos. Tres pruebas de instrumentación
comprueban la conversión, la omisión de caché privada y los extras no relacionados.
Esta incompatibilidad del hint está confirmada en el código; no se atribuye a
ella el cierre de los menús del dispositivo sin su traza.

## Cambio de ciclo de vida

AgbplayActivity hereda de la Activity oficial de Qt. Inicio y pantalla apagada
conservan su funcionamiento y el servicio multimedia. Al destruirse realmente
la Activity (fuera de cambios de configuración) o al quitar la tarea, se pide
una terminación idempotente. `stopWithTask=false` permite recibir onTaskRemoved.
Se liberan foco/wake lock/notificación y se encola una petición JNI de cierre,
independiente de las generaciones de comandos de transporte.

En el hilo Qt, esa petición detiene el transporte, cancela importaciones y
diálogos diferidos, guarda ajustes si es posible y pide el cierre normal de Qt.
closeEvent acepta el cierre cuando ya se está terminando, sin preguntas, también
si llega después otro Quit de QtActivity. Los destructores normales
liberan el motor y esperan los workers. No se fuerza una señal ni killProcess
como mecanismo de cierre. Los perfiles pendientes no se sobrescriben
automáticamente al eliminar la tarea; el guardado explícito normal se conserva.

Se configura `QT_ANDROID_NO_EXIT_CALL=1`, opción leída en
`androidjnimain.cpp::startQtNativeApplication` de Qt 6.11.2: la Activity de Qt
conserva la responsabilidad de salir del proceso después de finalizar Qt. Se
evita que el `exit(ret)` nativo y el `System.exit(0)` de QtActivity compitan.
El recorrido inicial de CI detectó `FORTIFY: pthread_mutex_lock called on a
destroyed mutex`/SIGABRT en hwuiTask0 durante esa terminación. La prueba exige
ausencia de fatal Java/nativo y salida registrada EXIT_SELF/0, no sólo que el
PID desaparezca. No se desactiva la aceleración gráfica ni se altera el audio.

El repintado vuelve a depender de Qt/exposición de superficie. El visualizador
sigue sin actualizarse en segundo plano y el control mantiene sus intervalos
16/250 ms. Se mantiene una solicitud de repaint encolada al volver; no se
ocultan/muestran ventanas ni se recrea el sintetizador para refrescar la pantalla.

## Pruebas

- Linux: flags de ventanas y diálogos, tareas diferidas ante Active/Hidden
  rápidos, eliminación de un perfil pendiente sin preguntas, idempotencia y
  fallo al guardar configuración. Se mantienen todas las suites anteriores.
- `scripts/test-android-lifecycle.py`: AudioTrack real, pantalla apagada, pausa,
  vencimiento del servicio, callbacks retirados antes/después de crear otra
  sesión, toques reales en Preferencias y tres aperturas/cancelaciones del
  selector SAF externo. Elimina la tarea con AppTask.finishAndRemoveTask estando
  oculta, reproduciendo y pausada con servicio vencido, y con perfil pendiente;
  exige liberación del proceso/servicio/wake lock y relanzamiento utilizable.
- La expiración por defecto se acelera invocando el Runnable real de cinco
  minutos. La reflexión está **sólo en el APK de pruebas**; verify-apk comprueba
  que el APK de distribución no contiene esa clase.
- El workflow conserva los recorridos de ambos modos de audio, foco, GSF y
  controles de pistas, además de los escenarios de interfaz teléfono/tablet.

Para esperar el plazo de pausa real y ampliar la escucha en el emulador API 35,
sobre el APK x86_64 del probe:

```bash
python3 scripts/test-android-lifecycle.py --screen-off-seconds 600 --real-expiry
```

Eso no simula todas las políticas de suspensión de Huawei. Las pruebas con
pantalla apagada durante horas y la recuperación del cierre original requieren
repetir el escenario en la tablet física.

## Capturar el fallo sin reinstalar primero

La Activity registra ciclo de vida y resultados del selector (sin URI ni nombre
del documento) en `agbplay.lifecycle`. Desde API 30 registra el historial de
salida del proceso que permite Android. Un handler Java conserva
`files/last-java-crash.txt` en almacenamiento privado y encadena el handler del
sistema; no sustituye debuggerd ni captura las trazas nativas por sí solo.

Con depuración USB y ADB, inmediatamente después del fallo:

```bash
./scripts/collect-android-lifecycle.sh despues-pausa
```

Guarda crash buffer, logcat, historial de salida cuando existe, estado de
Activity/servicio/ventanas, versión y último fallo Java en
`lifecycle-reports/despues-pausa/`. Se mantiene local y fuera de Git. El script no
borra datos, caché, permisos, configuración ni archivos del usuario.
