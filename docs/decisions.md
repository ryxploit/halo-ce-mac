# Decisiones técnicas

Formato: fecha, decisión, motivo y consecuencias. Las decisiones no se borran:
si cambian, se añade una nueva que reemplaza a la anterior.

## D1 · 2026-10-08 · Motor ILP32 en una arena de 4 GiB (no LP64)

- **Decisión:** el invitado sigue siendo ILP32 (`arm64_32`) y vive en una arena
  de 4 GiB alineada a 4 GiB dentro del proceso nativo. Todo acceso de memoria
  del invitado se reescribe con `tools/macos_asm_lift.py`.
- **Motivo:** el motor depende de punteros de 32 bits en los datos (mapas,
  partidas guardadas, layout de memoria del Xbox). Convertirlo a LP64 cambiaría
  el formato de todo eso y el de las partidas de red. Un proceso macOS arm64
  no puede mapear direcciones por debajo de 4 GiB (`port/macos/spike/lowmem_probe.c`).
- **Consecuencias:** el motor no cambia para macOS; el coste está en la
  reescritura del ensamblador y en el hilo que cruza el límite del invitado.
  La técnica viene del port de iOS de Nicholas Dominici (CC0).

## D2 · 2026-10-08 · Hypervisor.framework descartado como camino principal

- **Decisión:** no usar una máquina virtual. Queda como probe documentado
  (`port/macos/spike/hvf_probe.c`).
- **Motivo:** funciona (0,81 µs por salida al host), pero exige el entitlement
  `com.apple.security.hypervisor` y añade una capa más de depuración. La arena
  no necesita entitlement y las llamadas al host son nativas.
- **Consecuencias:** `hvf_probe` se mantiene solo como prueba de viabilidad.

## D3 · 2026-10-08 · Macro `HALO_GUEST` para el ABI del invitado

- **Decisión:** donde `HALO_ANDROID` significaba "compilado para el invitado
  ILP32", el código usa `HALO_GUEST`. Android define ambas. Linux y Windows no
  definen ninguna.
- **Motivo:** Android y macOS comparten el invitado; sin esta separación,
  macOS heredaba comportamiento de dispositivo móvil.
- **Consecuencias:** 14 archivos cambiados. Verificado con `unifdef`: preprocesan
  igual en Android, Linux y Windows. Los cambios de macOS específicos van con
  `HALO_MACOS` o `__APPLE__`.

## D4 · 2026-10-08 · Renderizado: GLES del motor sobre OpenGL 4.1 core

- **Decisión:** el invitado usa el camino OpenGL ES 3 del renderizador
  (`HALO_GLES`). El host crea un contexto OpenGL 4.1 core y lo presenta al
  juego como ES 3.0 (`host_gl.c`). Los shaders `#version 300 es` se reescriben
  a `#version 410 core`.
- **Motivo:** macOS no tiene OpenGL ES. De las 103 funciones del camino ES, 101
  existen en GL 4.1. ANGLE sobre Metal queda como alternativa si GL 4.1 no basta.
- **Consecuencias:** `glCopyImageSubData` y `glInvalidateFramebuffer` no están
  (se degradan). Los índices se reescriben en CPU donde hace falta.

## D5 · 2026-10-08 · Distribución: DMG firmado ad hoc, sin Developer ID

- **Decisión:** el app va firmado ad hoc (`codesign --sign -`). No se
  notariza hasta que el propietario aporte sus credenciales de Apple Developer.
- **Motivo:** no hay credenciales en el repositorio ni en el entorno. El
  Gatekeeper avisa la primera vez; las notas del release lo explican.
- **Consecuencias:** cada usuario debe permitir el app (Privacidad y seguridad
  o `xattr -dr com.apple.quarantine`). Firmar y notarizar son pasos separados
  (`docs/macos-release.md`).

## D6 · 2026-10-08 · Ningún dato del juego en el repositorio ni en el DMG

- **Decisión:** los mapas se extraen de la imagen de disco del jugador; el
  app los pide la primera vez y los guarda en `~/Library/Application Support/Halo CE`.
- **Motivo:** AGENTS.md prohíbe distribuir recursos propietarios.
- **Consecuencias:** un Mac nuevo necesita su ISO una vez. No hay forma de
  saltárselo sin distribuir los mapas.

## D7 · 2026-10-09 · La lista de brokers viaja con el app y se copia al arrancar

- **Decisión:** `port/assets/network/brokers.txt` va en `Contents/Resources`
  y el host la copia a la carpeta de datos en cada arranque (`host_main.c`).
- **Motivo:** el juego busca los brokers junto a `config.toml`. Sin el archivo,
  no hay partidas públicas ni invitaciones. Linux, Windows y Android ya lo hacían.
- **Consecuencias:** `tools/macos_bundle.py` falla si el archivo no existe.

## D8 · 2026-10-09 · Versión de protocolo de red: 21 (pendiente de revisar)

- **Decisión:** el fork se queda en `HALO_PORT_NETWORK_VERSION 21`.
- **Motivo:** la fusión con el upstream está pendiente porque tiene más de 20
  conflictos, sobre todo en el sistema de Custom Edition (dos implementaciones
  distintas del mismo código).
- **Consecuencias:** el navegador de partidas solo muestra partidas 21. En los
  brokers hay 228 listados legibles (escucha de 25 s en `broker.hivemq.com`):
  176 de la versión 24 (upstream actual), 34 de la 11, 6 de la 20, 6 de la 18 y
  6 de la 21 (nuestras). **Ver `docs/tasks.md`: pendiente de decisión.**

## D9 · 2026-10-09 · Un solo canal de descarga: GitHub Releases

- **Decisión:** los DMG se publican solo como release del repositorio. Se
  eliminó el paquete de GitHub Container Registry.
- **Motivo:** el propietario quiso un único sitio de descarga. GitHub Packages
  no acepta DMG como paquete normal; el registro de contenedores era un
  workaround.
- **Consecuencias:** el release `macos-v0.1.1` es el último. El release
  `macos-v0.1.0` se retiró porque su DMG ya no coincidía con sus notas.

## D10 · 2026-10-09 · Versionado del app

- **Decisión:** `CFBundleShortVersionString` en `port/macos/Info.plist` es la
  versión visible. Cada cambio visible sube la versión y se publica un release
  con tag `macos-v<versión>`. `CFBundleVersion` sube en cada versión.
- **Motivo:** el propietario pidió actualizar la versión con cada cambio.

## D11 · 2026-10-09 · README raíz solo de macOS

- **Decisión:** `README.md` describe únicamente el port de macOS. Se eliminó
  `port/macos/README.md` (duplicado). Los créditos al upstream van al final.
- **Motivo:** el propietario quería que la portada mostrara su trabajo de macOS.
- **Consecuencias:** las instrucciones de Linux, Windows y Android siguen en
  sus `port/*/README.md`, sin enlace desde la portada.

## D12 · 2026-10-09 · Instalador del DMG con diseño de Finder

- **Decisión:** el DMG tiene fondo propio con flecha, iconos colocados (600 × 400),
  icono del volumen y vista de iconos. Se configura con Finder sobre una copia
  escribible, y esa copia es la que se comprime.
- **Motivo:** la experiencia de instalación debía ser clara para el usuario.
- **Consecuencias:** requiere permiso de automatización de Finder desde el
  terminal. Si Finder lo rechaza, el DMG sale con el diseño sencillo y avisa.
  Finder borra el icono del volumen durante su pasada: el icono se copia y se
  marca después (`tools/macos_dmg.py`).

## D13 · 2026-10-09 · Conflicto de Custom Edition: pendiente

- **Decisión:** no tomada. Se espera la elección del propietario entre la
  implementación del fork y la del upstream para los 8 archivos `add/add`
  (`port/linux/game/cache_file_formats.*`, `custom_edition_*`,
  `port/tools/cache_file_report.c`, `tools/test_cache_file_formats.py`,
  `docs/custom_edition_caches.md`).
- **Consecuencias:** la sincronización con el upstream v24 está parada (ver D8).
