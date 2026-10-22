# Estado de validación

Informe provisional durante la construcción del port. No se ha usado un dispositivo
Android real. Las pruebas de hardware, latencia física y fidelidad acústica quedan
pendientes; véase `ANDROID.md`.

Verificado inicialmente en Linux x86_64:
- Código oficial completo publicado, con historial original.
- Motor sin AVX2 compilado con GCC 14.
- Pruebas `safety` y `playback` aprobadas.

En curso: pruebas adicionales de archivos/exportación, sanitizadores, escritorio
Qt6 y construcción Android con NDK 27.2 y Qt 6.11.2 desde fuentes oficiales.
