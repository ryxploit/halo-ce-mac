# Tareas

Estado: 2026-10-10. Prioridades: **P0** bloquea el uso o la distribución;
**P1** necesario para la primera versión estable; **P2** mejora.

## Pendientes

| Pri. | Tarea | Notas |
| --- | --- | --- |
| P0 | Decidir la versión de Custom Edition y sincronizar con el upstream (protocolo 24) | Ver D8 y D13. Sin esto, el navegador de partidas no muestra a la mayoría de jugadores. Hacerlo en una rama aparte, con las pruebas de macOS en cada paso. |
| P0 | Comprobar en otro Mac que el navegador de partidas muestra partidas públicas de la comunidad | Requiere la versión 24 del protocolo (tarea anterior). |
| P1 | Probar el audio en macOS | El stream SDL se abre sin errores; nadie lo ha escuchado. |
| P1 | Probar mandos en macOS | Código de SDL compartido; no verificado. |
| P1 | Probar partida en red (cliente y host) entre dos equipos | Solo se ha probado el listado con dos copias en un mismo Mac. |
| P1 | Firma Developer ID y notarización del app y del DMG | Requiere credenciales del propietario; no se hace sin ellas. Probar hardened runtime. |
| P1 | Compilar y probar Android, Linux y Windows tras el cambio `HALO_GUEST` | Verificado solo con `unifdef`; no se ha compilado aquí (faltan NDK y sysroots). |
| P2 | Probar en otros modelos de Mac y versiones de macOS | Solo probado en MacBook Pro M2 Pro, macOS 27.0.1. |
| P2 | Quitar el remoto `cyber` de `.git/config` si ya no hace falta | Se añadió para la comparación con el upstream. |
| P2 | Revisar `tools/test_linux_port.py` en macOS | 3 de 13 fallan por ser del target Linux (ver `macos-port-audit.md`). |
| P2 | Reducir el consumo de memoria del hilo de carga | Medido: 407 MB residentes en un nivel. Sin problema conocido. |

## Completadas

### Port de macOS (2026-10-08 a 2026-10-10)

- [x] Auditoría de viabilidad (`docs/macos-port-audit.md`).
- [x] Probes: memoria baja, Hypervisor.framework, técnica de arena.
- [x] Invitado ILP32 compilado y enlazado (`ninja macos_guest`).
- [x] Host arm64 con arena, cargador, hilos, syscalls, SDL, OpenGL y audio.
- [x] El motor arranca, carga mapas desde la imagen del jugador y juega un nivel.
- [x] Teclado y ratón verificados en menús (flechas, Intro, Escape, clic, cierre desde el menú del app). Dentro de partida, F11, F12 y los controles de juego no se han probado.
- [x] Modos de pantalla: ventana, sin bordes y pantalla completa.
- [x] App `Halo CE.app` con icono, Info.plist, licencias y firma ad hoc.
- [x] DMG con diseño de instalador e icono de volumen.
- [x] 22 pruebas automáticas (`tools/test_macos_port.py`).
- [x] Documentación: `docs/macos-build.md`, `docs/macos-controls.md`, `docs/macos-release.md`, `docs/macos-port-audit.md`.
- [x] Lista de brokers incluida y copiada al arrancar (partidas públicas visibles en el log).
- [x] Release `macos-v0.1.1` publicado; `macos-v0.1.0` y el paquete de ghcr retirados.
- [x] README raíz solo de macOS.
- [x] Esta memoria del proyecto (`CLAUDE.md`, `docs/`).

## Próximos pasos recomendados

1. Elegir la versión de Custom Edition (D13) y ejecutar la sincronización en una rama.
2. Probar el navegador de partidas entre dos Macs con la versión sincronizada.
3. Probar audio y mandos a mano y anotar el resultado en `docs/macos-controls.md`.
4. Cuando el propietario aporte sus credenciales de Apple Developer: firmar, notarizar y publicar la versión siguiente.
