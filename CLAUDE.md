# CLAUDE.md

Punto de entrada para cualquier desarrollador o instancia de Claude que trabaje
en este repositorio. Lee este archivo y después `docs/`, en este orden:

1. `AGENTS.md`: reglas de trabajo y de alcance que el propietario ha fijado
   (en español). Tienen prioridad sobre las convenciones de este archivo.
2. `docs/architecture.md`: cómo está organizado el código y cómo fluyen los datos.
3. `docs/decisions.md`: por qué las cosas son como son. Consúltalo antes de
   cambiar un mecanismo.
4. `docs/tasks.md`: qué falta y en qué orden.
5. `docs/roadmap.md`: objetivos a corto, medio y largo plazo.
6. `docs/session-log.md`: qué se hizo en cada sesión y qué quedó pendiente.

Los detalles de cada plataforma están en `docs/macos-*.md` (port de macOS)
y en `port/<plataforma>/README.md` (Linux, Windows, Android).

## Proyecto

Port de la decompilación de Halo: Combat Evolved (build Xbox 2342,
`cachebeta.exe`). Plataformas:

| Plataforma | Estado | Dónde |
| --- | --- | --- |
| Linux, Windows | Compila (ver sus README) | `port/linux`, `port/windows` |
| Android (arm64) | Compila con NDK | `port/android` |
| macOS (arm64, Apple Silicon) | Funciona: el juego arranca, carga mapas y juega | `port/macos`, `tools/macos_*.py` |

Es un fork de `ryxploit/halo-ce-mac`, derivado de
`cybersecurity/halo-ce-universal` (upstream). El fork está en la versión de
protocolo de red **21**; el upstream ya está en la **24** (ver `docs/tasks.md`).

Los datos del juego (mapas, imágenes de disco) **no** están en el repositorio
ni se distribuyen. Cada usuario aporta su propia imagen de disco.

## Stack

- **Motor y platform layer:** C. El juego se compila con `gnu89` y la capa de
  plataforma con `gnu11`. Las estructuras siguen el layout de 32 bits del Xbox.
- **Build:** Python 3 (`configure.py`) genera `build.ninja`; se compila con
  **ninja**. No hay CMake.
- **Compilador:** clang. Para Linux y Windows, el target i686. Para Android y
  macOS, el target `arm64_32-apple-watchos` (código AArch64 con punteros de 32 bits).
- **Gráficos:** OpenGL 4.5 en Linux/Windows; OpenGL ES 3.x en Android; OpenGL
  4.1 core presentado como ES 3.0 en macOS (`port/macos/host/host_gl.c`).
- **Sistema e I/O:** SDL3 (ventana, eventos, audio, mandos). Red por brokers
  MQTT (`port/linux/src/p2p_signal.c`). C library del invitado: musl 1.2.5.
- **Herramientas auxiliares:** Python 3 (tests `tools/test_*.py`, pytest),
  `hdiutil`, `codesign`, `osascript` (macOS).

## Reglas de desarrollo

- **Verifica antes de afirmar.** Ejecuta el comando o la comprobación y da el
  resultado real. Distingue lo probado, lo no probado y lo bloqueado.
- **No hagas commits, pushes ni releases sin autorización explícita** (AGENTS.md).
  Cuando se autoriza, el flujo usado es rama → PR → fusión, nunca directo a `main`.
- **Nunca** pongas datos del juego, certificados, contraseñas ni tokens en el repositorio.
- **Sin telemetría ni servicios nuevos** (AGENTS.md).
- **No cambies código de otra plataforma** para que compile la tuya. Si el
  cambio es compartido, usa las macros de plataforma (`HALO_GUEST`,
  `HALO_MACOS`, `__APPLE__`, `HALO_ANDROID`) y comprueba que Linux y Android
  preprocesan igual (`unifdef`).
- Actualiza `docs/` cuando un cambio sea relevante: arquitectura, decisión,
  tarea completada o sesión de trabajo.

## Estándares de código

- **C del juego y de las plataformas:** tabuladores, llaves en línea propia,
  nombres en `snake_case`, funciones de plataforma con prefijo del módulo
  (`posix_*`, `host_*`, `platform_*`, `xiso_*`). Los comentarios explican el
  porqué y se escriben en inglés, como en el resto del código del juego.
- **Herramientas Python:** Python 3 estándar, docstring al inicio con el uso,
  sin dependencias nuevas sin justificarlo (Pillow solo para el icono).
- **Targets de ninja** de macOS con prefijo `macos_`. Los archivos generados
  van bajo `build/` (ignorado por git).
- **Documentación de usuario** en español; comentarios de código en inglés.

## Restricciones arquitectónicas

- El motor es ILP32: **no** se convierte a LP64 (rechazado, ver `docs/decisions.md`).
- En macOS el invitado vive en una arena de 4 GiB alineada a 4 GiB; la base va
  en `x27` y todo acceso de memoria pasa por `tools/macos_asm_lift.py`.
- En macOS no se escribe dentro del bundle `.app`; los datos van a
  `~/Library/Application Support/Halo CE`.
- La lista de brokers (`port/assets/network/brokers.txt`) debe llegar a la
  carpeta de datos: el bundle la incluye y el host la copia en cada arranque.

## Comandos habituales

```sh
python3 configure.py --release --pgo=off    # configurar (macOS: sin descargas implícitas)
ninja macos_app                             # build/macos/Halo CE.app
ninja macos_dmg                             # build/macos/Halo-CE-macOS-<versión>-arm64.dmg
python3 tools/macos_fetch.py                # musl y cabeceras Khronos (una vez)
.venv-macos/bin/python -m pytest tools/test_macos_port.py
```
