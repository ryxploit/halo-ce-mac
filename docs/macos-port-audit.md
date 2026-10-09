# Auditoría de viabilidad: puerto macOS

Fecha de auditoría: 2026-10-08. Estado actualizado: 2026-10-09.

## Estado actual (resumen)

El motor real se ejecuta en macOS arm64. El juego arranca, extrae `maps/` de
la imagen de disco del jugador, muestra los menús y ejecuta niveles de
campaña con IA, física, cinemáticas y HUD. El renderizado se ha comprobado
con capturas del propio renderizador en el nivel `a30`.

La arquitectura es la de "Decisión de arquitectura" (más abajo): el invitado
ILP32 del port de Android, en una arena de 4 GiB del propio proceso, con la
técnica del port de iOS. El renderizador es el camino OpenGL ES del motor
sobre OpenGL 4.1 core.

| Pieza | Dónde |
| --- | --- |
| build del invitado | `tools/macos_guest_build.py`, `tools/macos_asm_lift.py`, `port/macos/guest.ld`, `port/macos/guest/` |
| host | `port/macos/host/`, `tools/macos_host_build.py`, `tools/macos_host_bridges.py`, `tools/macos_embed_guest.py` |
| app y DMG | `tools/macos_bundle.py`, `tools/macos_dmg.py`, `tools/macos_icon.py`, `port/macos/Info.plist` |
| dependencias | `tools/macos_fetch.py` (musl y cabeceras Khronos, con SHA-256) |
| tests | `tools/test_macos_port.py` (22 tests; ver `macos-build.md`) |

Cambios en código compartido con otras plataformas, todos sin efecto fuera
de macOS:

- **Separación ABI/dispositivo.** `HALO_ANDROID` → `HALO_GUEST` donde
  significaba el ABI del invitado: 7 archivos de `source/` con
  `#pragma bss_seg`, más `source/cseries/stack_walk_windows.c`,
  `source/hs/hs.c`, `port/linux/include/halo_linux_prefix.h`,
  `port/linux/src/msvc_crt.c` y `port/linux/src/gl.h`. En
  `port/linux/src/gles_desktop.c` y `port/linux/src/xbox_textures.c` la
  condición `HALO_GLES && !HALO_ANDROID` pasa a `HALO_GLES && !HALO_GUEST`.
  El build Android añade `-DHALO_GUEST=1` (`tools/android_build.py`). Se
  comprobó con `unifdef` que los 14 archivos preprocesan igual que en `HEAD`,
  tanto con `HALO_ANDROID` y `HALO_GUEST` definidas (Android) como sin
  ninguna (Linux, Windows), resolviendo también `__i386__` y `HALO_GLES`.
- **Bloques `HALO_MACOS`**, que sólo define el invitado de macOS:
  `port/linux/src/updater.c` (sin auto-actualizador: no hay releases de
  macOS) y `port/linux/src/xbox_files.c` (partidas en la carpeta de usuario).
- **Bloques `__APPLE__`** en los helpers que corren en el host:
  `port/linux/src/posix_files.c` (nombres de los tiempos de `stat`) y
  `port/linux/src/posix_net.c` (`SOCK_CLOEXEC`, `accept4`, `getrandom`, la
  cabecera de `sockaddr` BSD y no registrar esquemas de URL). Las llamadas
  usan macros `ADDRESS_*` que fuera de Darwin se expanden a la expresión
  original con un cast. **No se ha podido compilar `posix_net.c` para Linux en
  esta máquina** (no hay sysroot de Linux).

No se han compilado aquí Android, Linux ni Windows: faltan el NDK y los
sysroots. Los tests de herramientas existentes (`tools/test_linux_port.py`)
dan el mismo resultado que antes de los cambios (ver "Verificación
ejecutada").

Pendiente o no verificado: audio audible, mandos, juego en red, otros Macs y
versiones de macOS, firma Developer ID y notarización, hardened runtime, y
el `base vertex` y `copy image` de OpenGL (desactivados: el renderizador
reescribe los índices en CPU).

## Alcance e identidad del checkout

Este checkout está en `main`, commit `a3fa6eeca74add0b9f66469f7b47ebcb8cdfceba`
(`Merge cybersecurity/halo-ce-universal main (07302a86)`). `origin` apunta a
`ryxploit/halo-ce-mac` y el remoto `upstream` configurado apunta a
`bnunu/halo-ce-universal`. Al inicio de la auditoría, el README identificaba
Linux, Windows y Android como sus únicas plataformas y no existía un target
macOS en `configure.py`, `tools/ci_build.py` ni `.github/workflows/build.yml`.
Como primer hito de esta auditoría se añadió después el target aislado
`macos_spike`, que hoy sólo construye `build/macos/macos_spike`; el port
completo está en `macos-build.md`.

Había un único cambio local antes de esta auditoría: `AGENTS.md`, sin seguir.
No se ha modificado ni descartado.

## Arquitectura y arranque actuales

El juego reconstruido es C bajo `source/`. `port/linux/port.json` enumera ese
árbol como código de juego y combina una capa de plataforma de
`port/linux/src` y los adaptadores de juego de `port/linux/game`. Linux y
Windows son ejecutables nativos de 32 bits; el README y
`tools/linux_build.py` lo documentan y fuerzan `--target=i686-linux-gnu`,
`-m32`, `-fshort-wchar`, `-malign-double` y convenciones de ABI compatibles
con MSVC/Xbox. `port/include/xdk/` suministra las declaraciones XDK que el
motor consume.

El punto de entrada y el bucle de plataforma de escritorio están en la capa
compartida de Linux: `port/linux/src/sdl_platform.c` inicializa SDL,
crea la ventana y contexto, bombea eventos en el hilo principal y presenta
frames. `xinput_sdl.c` convierte el estado SDL en los mandos Xbox esperados
por el motor. Las implementaciones de Xbox/Win32 (archivos, memoria, red y
audio) viven principalmente en `port/linux/src/xbox_*.c`, `posix_*.c`,
`dsound_sdl.c` y `xnet.c`.

Android no ejecuta el motor con el ABI normal arm64 de su proceso. Construye
el motor como invitado AArch64 ILP32 (punteros de 32 bits) y lo enlaza en una
imagen ELF fija; un host arm64 de Android carga la imagen y expone SDL, GLES,
archivos, red, hilos y memoria mediante una tabla de imports. Véanse
`tools/android_build.py`, `port/android/guest/guest.ld`,
`port/android/host/host_loader.c` y `port/android/host/host_main.c`.

## Lenguajes, build y dependencias

- El motor y las capas nativas son C. El build se genera con Python en
  `configure.py` y Ninja; no hay CMake como sistema principal. Android añade
  Gradle para el APK.
- `tools/linux_build.py` genera el target `linux`; `tools/windows_build.py`,
  el target `windows`; `tools/android_build.py`, `android` y `android_apk`.
  `tools/ci_build.py` y `.github/workflows/build.yml` cubren únicamente esos
  tres destinos.
- SDL3 proporciona ventana, eventos, mandos y audio (`sdl_platform.c`,
  `xinput_sdl.c`, `dsound_sdl.c`). Las dependencias integradas incluyen
  tomlc17, Expat, KCP, Monocypher, musl-math, zlib, mbedTLS y miniupnpc;
  están referenciadas en `tools/linux_build.py`.
- Android descarga SDL3 y musl durante configuración (`fetch_third_party` en
  `tools/android_build.py`). Un target macOS no debe reutilizar esa descarga
  implícitamente ni instalar herramientas globales.

En esta máquina se verificaron macOS 27.0.1 arm64, Xcode 27.0 y Apple clang
21.0.0. No se detectaron `ninja` ni `pkg-config`/SDL3 en el `PATH`; por tanto
todavía no es posible validar una compilación macOS sin proveer esas
dependencias de forma explícita.

## Código reutilizable y dependencias de Android

La mayor parte de `source/`, `port/linux/game` y la capa SDL de
`port/linux/src` es candidata a reutilización lógica. En particular, ya hay
implementaciones de ventana redimensionable, pantalla completa, captura y
liberación del cursor, foco, teclado, ratón, wheel y mandos en
`sdl_platform.c`; el mapeo y la reasignación existen en `xinput_sdl.c` y
`tools/port_settings.py`. El README de Linux documenta los controles reales
en `port/linux/README.md`.

No son reutilizables directamente en macOS los componentes Android:

- `port/android/app/` (Java, manifiesto, Gradle y recursos APK);
- `port/android/host/` (NDK, JNI/SDLActivity, logcat y almacenamiento
  Android);
- `port/android/guest/` y el subconjunto de musl (runtime ILP32, syscalls y
  script ELF);
- los bridges de punteros y GL de `guest_sdl.c`, `host_sdl.c` y los
  generados por `tools/android_gl_stubs.py`.

## ABI, memoria y ejecución

Este es el riesgo principal. El motor no es portable de forma mecánica a un
proceso macOS arm64 LP64:

- El build Linux conserva ABI de 32 bits, `wchar_t` de 16 bits y layout MSVC
  (`LINUX_ABI_FLAGS` en `tools/linux_build.py`). Muchos datos serializados y
  declaraciones XDK dependen de ello.
- macOS moderno no ejecuta procesos i386, y arm64 normal usa punteros de 64
  bits. Compilar `source/` directamente como arm64 cambiaría tamaños, layout
  y convenciones de llamada.
- El juego y el renderizador suponen una ventana física fija de 128 MiB en
  `0x80000000`: `PLATFORM_CONTIGUOUS_BASE` y
  `PLATFORM_CONTIGUOUS_SIZE` en `port/linux/src/platform.h`. La reserva y
  asignación con `mmap` están en `port/linux/src/xbox_memory.c`; el
  seguimiento de escrituras protege esas páginas con `mprotect` en
  `memory_watch.c`.
- Android resuelve el requisito al usar `arm64_32-apple-watchos` sólo como
  compilador de código ILP32, convertir el ensamblador a ELF y ejecutar una
  imagen situada desde `0x88000000` (`tools/android_build.py` y
  `port/android/guest/guest.ld`). Su host exige que toda la imagen quede por
  debajo de 4 GiB (`host_loader.c`). No es un binario Mach-O que macOS pueda
  ejecutar ni cargar directamente.

Antes de prometer un ejecutable macOS debe comprobarse experimentalmente que
el sandbox/proceso macOS puede reservar sin reemplazar mapeos ajenos los
rangos bajos necesarios, y que se puede construir y llamar de forma segura a
un invitado ILP32. Esa comprobación aún no existe en el repositorio.

## Renderizador, audio, entrada, archivos y red

El backend actual traduce Direct3D 8/Xbox a OpenGL. En escritorio pide un
contexto OpenGL core 4.5; con `--gles` pide OpenGL ES 3.2
(`sdl_platform.c:680-757` y `gl.h`). `d3d8_gl.c` usa, entre otros,
`glClipControl`, `glGetTexImage` y semántica de OpenGL de escritorio. macOS
no ofrece OpenGL 4.5 y su OpenGL decompatibilidad no equivale a GLES 3.2;
por ello el backend OpenGL de escritorio no se puede seleccionar sin una
prueba de funciones. El camino GLES existente también requiere validación en
macOS (por ejemplo mediante un proveedor GLES/ANGLE que sea distribuible),
no una suposición de equivalencia.

El audio está implementado como mezclador software sobre SDL3 en
`port/linux/src/dsound_sdl.c`. La entrada y los mandos son SDL3. Los archivos
Xbox se traducen mediante `xbox_files.c`; en Linux, `d:\` busca `maps/` en
`paths.data`, en el directorio actual o en `assets/` junto al ejecutable, y
los guardados van a `paths.saves`, `$XDG_DATA_HOME/halo-linux` o
`~/.local/share/halo-linux` (`platform_save_root`, `xbox_files.c`). Para macOS se debe cambiar
únicamente la política de rutas de la nueva capa, usando `Application
Support` para datos/guardados/configuración y nunca `Contents/` del bundle.
La selección de una imagen ISO/XISO ya existe en SDL en
`platform_offer_game_data` (`sdl_platform.c`); hay que adaptarla a un destino
de usuario macOS. Red, KCP, UPnP y actualización residen en `p2p*.c`,
`posix_net.c`, `posix_upnp.c` y `posix_update.c`; ningún nuevo servicio es
necesario para un port inicial.

## Pruebas, comandos y licencias actuales

Los comandos documentados son `python configure.py` seguido de `ninja linux`,
`ninja windows` o `ninja android_apk` (README). `tools/ci_build.py` replica
las compilaciones de CI. `tools/test_linux_port.py` contiene pruebas pytest
para tooling, headers, assets y validadores; no hay smoke test ni CI macOS.
No se ejecutó una compilación: Ninja y SDL3 no están disponibles en este
entorno y no se descargaron ni instalaron dependencias.

La licencia raíz es CC0-1.0 (`LICENSE.md`). Las bibliotecas incluidas tienen
licencias propias en sus subdirectorios `port/third_party/`. El README deja
claro que los datos del juego no se incluyen; un port macOS debe seguir
requiriendo que el usuario aporte una imagen/disco compatible y no puede
empaquetar assets propietarios ni presentarse como producto oficial.

## Decisión de arquitectura y puerta de viabilidad

Un target macOS arm64 que compile todo el motor como código arm64 LP64 queda
descartado: rompería los requisitos de ABI y memoria anteriores. Una ventana
SDL vacía no sería evidencia de que Halo funcione.

Las alternativas reales son:

1. **Host macOS arm64 + invitado AArch64 ILP32**, derivado del diseño Android:
   nueva capa `port/macos` para el bundle y servicios macOS, conservando el
   motor y adaptando el bridge de imports, cargador, hilos y memoria. Es la
   opción que mejor reutiliza el motor, pero requiere un spike que demuestre
   generación/carga de imagen ILP32 y reservas bajas en Darwin, además de un
   backend gráfico viable.
2. **Host macOS arm64 + emulación de un ejecutable i386 Linux/Windows**:
   reutilizaría el binario/capa de escritorio, pero introduce un emulador y
   distribución/complejidad ajenos al proyecto; no es una aplicación nativa
   macOS y no es la dirección recomendada.
3. **Convertir el motor a LP64 arm64**: requeriría una auditoría y cambios
   extensos de layouts, punteros, llamadas y datos persistentes. Es una
   reingeniería del motor, contraria al objetivo de una capa mínima, y no es
   recomendable como primer port.

La recomendación es la alternativa 1, empezando con un spike de viabilidad
aislado y sin tocar Android: prueba de reserva de memoria, llamada
host-invitado, SDL3 y backend gráfico. Es una decisión arquitectónica
irreversible respecto al formato de ejecución y debe confirmarse antes de
implementar el target, bundle y empaquetado.

## Riesgos bloqueantes con referencias

| Riesgo | Evidencia | Consecuencia |
| --- | --- | --- |
| ABI ILP32 obligatorio | `tools/linux_build.py` (`LINUX_ABI_FLAGS`); `tools/android_build.py` (`GUEST_ABI_FLAGS`) | Un target arm64 LP64 no puede ejecutar el motor sin una migración grande. |
| Direcciones fijas y memoria protegida | `port/linux/src/platform.h`, `xbox_memory.c`, `memory_watch.c` | Hay que validar mapeos bajos y manejo de fallos de página en macOS. |
| Formato Android no cargable por macOS | `port/android/guest/guest.ld`, `host_loader.c` | El guest Android no puede copiarse como artefacto macOS. |
| OpenGL insuficiente/no equivalente | `port/linux/src/sdl_platform.c`, `gl.h`, `d3d8_gl.c` | Hace falta validar GLES/ANGLE o diseñar otro backend antes de conectar el motor. |
| Dependencias de build ausentes | detección local: no `ninja`, no SDL3 accesible | No hay compilación macOS reproducible todavía. |
| Persistencia ligada al ejecutable | `port/linux/src/xbox_files.c` | La capa macOS debe redirigir datos y saves fuera del bundle. |

## Spike ILP32: resultado (2026-10-08) — bloqueado

Se probó la alternativa 1 en el orden que ella misma exige: primero la
memoria baja, luego el invitado. Reproducible con
`port/macos/spike/lowmem_probe.c`:

    clang -arch arm64 -std=c11 -Wall -Wextra -Werror -o build/macos/lowmem_probe port/macos/spike/lowmem_probe.c
    build/macos/lowmem_probe

Resultado en macOS 27.0.1 arm64 (`sizeof(void *) = 8`):

- `mmap(MAP_FIXED)` en `0x10000000`, `0x80000000`, `0x88000000`, `0xf0000000`
  y `0x100000000` falla con `ENOMEM`, tanto con el `__PAGEZERO` de 4 GiB como
  después de `munmap([0, 4 GiB))`.
- Una petición sin dirección devuelve `0x102590000`: el proceso no recibe
  ninguna dirección por debajo de 4 GiB.
- Reducir `__PAGEZERO` al enlazar (`-Wl,-pagezero_size,0x4000`) no sirve: un
  ejecutable trivial de 64 bits muere con SIGKILL (137) al arrancar.

Conclusión: un proceso macOS arm64 de 64 bits no puede alojar el espacio de
direcciones de 32 bits que el invitado ILP32 necesita. El invitado no llega a
cargarse ni a llamarse. El compilador sí funciona: `clang --target=arm64_32-apple-watchos`
genera objetos `arm64_32` válidos, pero eso no resuelve la memoria.

Esta es la puerta de decisión que `AGENTS.md` pide. Las alternativas que
quedan, todas sin verificar más allá de lo indicado:

1. **Convertir el motor a LP64** (la alternativa 3). Es el único camino que
   da una app nativa arm64 que ejecuta el motor en el propio proceso.
   Implica cambiar tamaños de punteros, layouts, la tabla de direcciones fijas
   (`PLATFORM_CONTIGUOUS_BASE`) y los formatos de datos persistentes. Es grande
   y afecta a compatibilidad de guardados y de red con las otras plataformas.
2. **Ejecutar el invitado fuera del proceso principal**, por ejemplo en una VM
   (Hypervisor.framework) o en un emulador de usuario. Comprobado después con
   `hvf_probe`: es viable; véase la sección siguiente.
3. **Emular el binario de 32 bits**. Ya descartado en esta auditoría: no es
   una app nativa.

Ningún cambio del motor se ha hecho. Cualquier avance de Hitos C-G depende de
elegir una de estas vías.

## Coste medido de convertir el motor a LP64 (2026-10-08)

Antes de empezar la conversión se midió su alcance en el código:

- `struct tag_block` (`source/tag_files/tag_groups.h:86`) guarda
  `void *address` y `struct tag_block_definition *definition`. Los datos de
  tags se cargan tal cual desde el mapa en `TAG_CACHE_BASE_ADDRESS`
  (`0x803A6000`, `source/cache/physical_memory_map.c`), con punteros
  absolutos de 32 bits. En LP64 cada estructura de tag con punteros cambia de
  tamaño, y habría que traducir todos los tags al cargar el mapa.
- El estado del juego vive en una base fija y las partidas guardadas son
  volcados de esa memoria (`game_state_write_core`, `source/saved games/game_state.c`).
- El motor usa `long` como entero de 32 bits; en LP64 de Darwin mide 64 bits.
  Hay unas 15.200 apariciones de `long` en `source/`, 1.800 en
  `port/linux/game` y 1.500 en `port/linux/src`.
- Hay 333 usos de `.address`/`->address` en `source/`, 115 en
  `port/linux/game` y 64 en `port/linux/src`.
- El código casi no tiene aserciones de tamaño (una sola `static_assert`), así
  que el compilador no detectaría los cambios de layout.
- El README de Android lo resume: los mapas tienen el layout de la memoria de
  Xbox, las partidas guardadas son copias de memoria y los recursos Direct3D
  tienen direcciones físicas de 32 bits (`port/android/README.md`, "ILP32 code").

Es una migración del motor compartido por todas las plataformas, con riesgo
para la compatibilidad de guardados y de red. Por eso no se empezó sin
comprobar antes la alternativa 2.

## Spike Hypervisor.framework (2026-10-08): viable

`port/macos/spike/hvf_probe.c` crea una VM de `Hypervisor.framework`, mapea
64 MiB de memoria del host en la dirección de invitado `0x80000000`, monta una
tabla de páginas identidad de 32 bits y ejecuta funciones de
`port/macos/spike/hvf_guest.c` compiladas para `arm64_32-apple-watchos`, el
mismo ABI que el invitado de Android. `tools/macos_guest_blob.py` copia la
sección `__text` del objeto y rechaza objetos con relocaciones.

    ninja macos_hvf_probe
    build/macos/hvf_probe

Resultado en macOS 27.0.1 arm64 (Apple clang 21.0.0), todo `PASS`:

- Tamaños ILP32: puntero y `long` de 4 bytes, nodo `{next, value}` de 8.
- Recorrido de una lista enlazada de punteros de 32 bits en `0x80010000`.
- Llamada invitado→host con `hvc` y retorno al invitado.
- FPU/SIMD en el invitado.
- Coste de una llamada al host: 0,81-0,86 µs (200.001 llamadas).
- Dos vCPU en dos hilos del host sobre memoria compartida: 1.000.000 de
  incrementos atómicos sin pérdidas.
- Un acceso del invitado a memoria sin respaldo llega al host como abort de
  stage 2 con la dirección exacta (`0x10000000`).

El binario se firma ad-hoc con `port/macos/hypervisor.entitlements`
(`com.apple.security.hypervisor`); sin ese entitlement `hv_vm_create` falla.

### Qué implica

El invitado de Android ya separa el motor del sistema: llama al host sólo con
stubs generados (`tools/android_imports.py`, `android_gl_stubs.py`,
`android_posix_stubs.py`) y su libc musl envía las syscalls al host. La
superficie es de 54 imports con nombre (`port/android/host_imports.list`),
216 funciones GL (`port/linux/src/gl.h`) y los stubs POSIX. El host de Android
tiene unas 2.500 líneas (`port/android/host/`). Un host macOS sobre una VM
reutiliza el invitado sin cambiar el motor; cambian el host, los stubs (`hvc`
en lugar de saltos directos) y la traducción de punteros del invitado a
direcciones del host.

Riesgos todavía sin verificar:

- **Rendimiento**: unas 3.000 llamadas GL por frame costarían unos 2,5 ms
  sólo en salidas a la VM. Si no basta, habrá que agrupar llamadas GL.
- **Granularidad de páginas**: la protección de stage 2 trabaja a 16 KiB, el
  tamaño de página del host. `memory_watch` del host Android tendría que
  adaptarse.
- **Hilos**: un vCPU sólo puede ejecutarse en el hilo que lo creó. Cada hilo
  del host que entra en el invitado (incluido el callback de audio de SDL)
  necesita su propio vCPU.
- **Build del invitado**: el invitado de Android necesita `ld.lld` y musl, que
  `tools/android_build.py` descarga (`fetch_third_party`). Para macOS hay que
  decidir cómo proveerlos sin descargas implícitas.
- **Distribución**: no se ha comprobado una app ad-hoc con este entitlement en
  otro Mac. Para distribuir hace falta firma Developer ID y notarización.

## Referencia externa: port iOS con arena ILP32 (2026-10-08)

El propietario señaló `NicholasDominici/halo-ce-ios`, rama `ios-port` (revisado
en `3f2c141`, solo lectura, sin ejecutar su código). Es CC0 1.0 como este
repositorio y declara la misma base (`cybersecurity/halo-ce-universal`). Su
árbol parte de nuestro commit `16514a13`, 331 commits por detrás de `HEAD`, y
su `port/linux` ha divergido mucho, así que no se puede copiar entero.

Resuelve exactamente el bloqueo de memoria baja, sin VM:

- Compila el invitado `arm64_32` con `-ffixed-x15 -ffixed-x27`.
- `tools/ios_asm_convert.py` reescribe el ensamblador: cada acceso a memoria y
  cada `br`/`blr` pasa por `mov w15, wN; orr x15, x15, x27`. `x27` es la base
  de una arena de 4 GiB alineada a 4 GiB; los punteros siguen siendo de 32
  bits y los layouts no cambian. `adr`/`adrp` se normalizan a offsets.
- Las pilas están dentro de la arena, así que `sp` y `x29` ya son direcciones
  nativas válidas; un puntero nativo de la arena truncado a 32 bits es su
  offset de invitado.
- Los stubs de imports no se reescriben: cargan con `adrp` la dirección nativa
  de la tabla de imports y saltan a funciones del host, que conserva `x27`
  porque es callee-saved. Bridges generados traducen los punteros de los
  argumentos (`host_pointer`).
- El código del invitado va embebido en el `__TEXT` firmado de la app y se
  alía ejecutable dentro de la arena con `vm_remap`; no hay JIT.
- Emula páginas de 4 KiB de Xbox sobre las de 16 KiB de Apple
  (`port/ios/host/host_memory.c`).

Comprobaciones en esta máquina:

- `vm_map` de 4 GiB alineados funciona en un proceso macOS sin entitlements:
  arena en `0x7000000000`, páginas de 16 KiB, y el puntero `0x803a6000` se
  traduce y accede.
- El clang de Apple compila `arm64_32` con `-ffixed-x15 -ffixed-x27` y
  `-mllvm -aarch64-enable-compress-jump-tables=false`, y ensambla objetos ELF
  de AArch64.
- `tools/android_asm_convert.py` y su `guest_asm_convert.py` son el mismo
  código (difieren 7 líneas de docstring). Su lista de imports es la nuestra
  menos 5 funciones de Android.

## Decisión de arquitectura (2026-10-08)

Se adopta la técnica de arena del port iOS sobre el invitado Android que ya
existe aquí. Sustituye a la conversión LP64 (no iniciada) y a la VM de
`Hypervisor.framework` (que queda como probe documentado):

| | LP64 | VM (`hvf_probe`) | Arena (port iOS) |
| --- | --- | --- | --- |
| Cambios en el motor | ~18.500 `long`, tags, saves | ninguno | ninguno |
| Coste por llamada al host | nativo | 0,81 µs por salida | nativo |
| Entitlement especial | no | `com.apple.security.hypervisor` | no |
| Probado en hardware Apple | no | probe aquí | iPhone/iPad (según su README) |

### Diseño del target macOS

- **Invitado**: el mismo pipeline que `tools/android_build.py` (C → asm Darwin
  → `android_asm_convert.py` → paso de arena → objeto ELF → `ld.lld`), con las
  fuentes del juego y de `port/linux/src` y el runtime/libc de
  `port/android/guest`. Sin cambios en el motor.
- **Separar ABI y dispositivo**: hoy `HALO_ANDROID` mezcla el ABI ILP32
  (`#pragma bss_seg` en 7 archivos de `source/`, el recorrido de pila,
  `hs.c`, FPCR en `msvc_crt.c`, GLES forzado en `gl.h`) con comportamiento de
  Android (táctil, rutas, actualizador, invitaciones, importador de ISO). Se
  propone una macro `HALO_GUEST` para el ABI que el build Android también
  defina, de modo que Android preprocese exactamente igual. El invitado macOS
  definirá `HALO_GUEST` y `HALO_MACOS`, y tomará las ramas de escritorio.
- **Host** (`port/macos/host/`, Objective-C/C arm64): arena, cargador por
  `vm_remap` del invitado embebido, hilos con pila en la arena, traducción de
  syscalls musl/Linux a Darwin, tabla de imports y bridges, SDL3 de escritorio.
  Se adapta del host iOS (CC0, con atribución) y del host Android de este
  repositorio.
- **Renderizador**: el camino GLES del renderizador (el de Android) sobre el
  OpenGL 4.1 core de macOS. De las 103 funciones de ese camino, 101 existen
  en el `gl3.h` del SDK de macOS; faltan `glCopyImageSubData` y
  `glInvalidateFramebuffer`. El bridge GL del host tiene que traducir los
  shaders (`#version 300 es`), emular o ignorar esas dos funciones y presentar
  versión y extensiones al renderizador. ANGLE sobre Metal queda como
  alternativa si GL 4.1 no basta; OpenGL está obsoleto en macOS aunque sigue
  presente en macOS 27.
- **Entrada**: las ramas de escritorio de `sdl_platform.c` y `xinput_sdl.c` ya
  tienen teclado, ratón, captura de cursor, foco y mandos. En el invitado esas
  llamadas a SDL pasan por imports; habrá que ampliar el shim SDL del
  invitado con las funciones de escritorio que falten.
- **Datos**: `Application Support/Halo CE` para configuración, guardados y
  mapas extraídos, nunca dentro del bundle.

### Dependencias nuevas que hacen falta

- `ld.lld` (Homebrew `lld`). Xcode no trae enlazador ELF. `llvm-ar` no es
  imprescindible si se usa `ld.lld --start-lib`.
- Fuentes de musl 1.2.5 (`musl.libc.org`) y cabeceras Khronos de GLES 3.2
  (`gl32.h`, `gl2ext.h`, `gl2platform.h`, `khrplatform.h`). El build Android
  descarga musl al configurar; para macOS se propone un script de descarga
  explícito, con revisiones fijas y sha256, en lugar de descargas implícitas.
- SDL3: la de Homebrew ya instalada (3.4.18), tanto para el host como para las
  cabeceras del invitado, para que los layouts coincidan.

### Hitos

1. Invitado macOS que compila y enlaza (`ninja macos_guest`).
2. Host que carga el invitado y llega a `main` con logs y errores claros.
3. Bridge GL sobre GL 4.1: menú principal visible.
4. Entrada de escritorio, resize, pantalla completa, foco.
5. Selector de ISO y almacenamiento en `Application Support`.
6. Bundle `Halo CE.app` con SDL3 embebida y rutas relativas.
7. DMG.

## Próximo hito propuesto (superado por el spike ILP32)

Tras elegir la alternativa de host macOS + invitado ILP32, crear un target
macOS independiente y un spike pequeño que falle con diagnósticos claros si
la reserva de memoria, carga del invitado o contexto gráfico no son posibles.
Sólo después de que ese spike sea reproducible conviene invertir en el
bundle `Halo CE.app`, controles y DMG.

## Verificación ejecutada en esta máquina (2026-10-08)

- `python3 configure.py --pgo=off`: se completa. Genera la regla
  `macos_spike`, que sin SDL3 falla con un mensaje de dependencia.
- `pytest tools/test_linux_port.py` (con pytest en un venv aislado, fuera del
  repositorio): 8 pasan, 3 se omiten por falta de `ninja`/maps y 2 fallan.
  Los dos fallos son tests del target Linux que no se adaptan a Darwin:
  - `test_xdk_headers_compile_for_the_game`: `halo_linux_prefix.h:95` incluye
    `<StdDef.h>` con otra capitalización (error con `-Werror` en APFS sin
    distinción de mayúsculas), y `ctype.h` no existe sin sysroot glibc i686.
  - `test_link_check_rejects_undefined_weak_references`: el validador de
    enlace asume semántica ELF y el resultado difiere con Mach-O.
  - Tras instalar `ninja`, `test_p2p_signatures_and_listings` deja de
    omitirse y también falla: copia las flags del objeto Linux, cuyo
    `-march=native` resuelve a `apple-m2`, una CPU desconocida para i686
    (`error: unknown target CPU 'apple-m2'`). Resultado actual: 8 pasan,
    3 fallan y 2 se omiten (necesitan los mapas en `assets/maps`).
- Dependencias instaladas con `brew install ninja pkgconf sdl3` (ninja 1.13.2,
  SDL3 3.4.18). Después se compiló `ninja macos_spike`: primero falló porque
  la regla no incluía las cflags de `pkg-config` (corregido en
  `tools/macos_build.py`); ahora compila y enlaza `build/macos/Halo CE.app`.
- `otool -L`: el binario arm64 enlaza `libSDL3` desde `/opt/homebrew`. Es
  válido para el spike de desarrollo, pero no es distribuible; un bundle
  final debe embebir SDL3 con rutas relativas.
- `halo-ce-macos-spike --smoke`: sale con código 0 sin errores de SDL.
- `halo-ce-macos-spike --bogus`: muestra el uso y sale con código 2.
- No se ha probado el motor ni la interacción con una ventana visible.
