# Registro de sesiones

Un bloque por sesión de trabajo, con lo implementado, lo corregido, los
problemas y las próximas acciones. Las decisiones se registran en
`docs/decisions.md`, no aquí.

## 2026-10-08 · Auditoría, spike y port de macOS

**Implementado**

- Auditoría de viabilidad (`docs/macos-port-audit.md`), con referencias al código.
- Target `macos_spike` (ventana SDL de primer hito) y probes de memoria baja y de Hypervisor.framework.
- Técnica de arena: pasada de ensamblador (`tools/macos_asm_lift.py`), invitado ILP32
  (`ninja macos_guest`) y host arm64 que ejecuta el motor.
- Separación `HALO_ANDROID` → `HALO_GUEST` (14 archivos), verificada con `unifdef`.
- Renderizado por OpenGL 4.1 presentado como ES 3.0.
- App `Halo CE.app`, DMG, licencias, icono, tests (22) y los cuatro documentos `docs/macos-*.md`.
- Partida de prueba de 60 s en el nivel `a30`, con capturas del renderizador (sin errores fatales).
- Release `macos-v0.1.0` (prerelease) con el DMG.

**Corregido**

- Shims de SDL del invitado: `SDL_GetBasePath`, `SDL_GlobDirectory`, `SDL_ShowOpenFileDialog`, `SDL_SetWindowFullscreenMode`, entre otros.
- Llamada `sysinfo` en el host (la pedía el motor al arrancar).

**Problemas encontrados**

- La memoria baja no es mapeable desde un proceso de 64 bits (`lowmem_probe`); motivó el rediseño.
- `ninja` y SDL3 no estaban instalados; se instalaron con Homebrew.
- La pasada de ensamblador rechazaba `stp x28, x27` en prólogos; se permitió ese caso.
- Las pruebas de `test_linux_port.py` fallan en macOS por ser del target Linux.

**Próximas acciones:** probar partida en red y audio; la versión 0.1.0 quedó pendiente de publicación oficial.

## 2026-10-09 · Partidas públicas, versión 0.1.1 e instalador

**Implementado**

- Lista de brokers incluida en el app y copiada al arrancar (D7).
- README raíz solo de macOS (D11).
- Ventana del instalador DMG con fondo, flecha, iconos colocados e icono de volumen (D12).
- Versión 0.1.1; release `macos-v0.1.1`; retirada de `macos-v0.1.0` y del paquete de ghcr (D9, D10).
- Descripción del repositorio y portada actualizadas.

**Corregido**

- Las partidas públicas no aparecían: faltaba `brokers.txt` en el app.
- El DMG final perdía la posición de los iconos: se comprime la copia que Finder configuró.
- Finder borraba el icono del volumen: se copia y se marca después de su pasada.
- Referencia duplicada en el fondo del DMG (`of disk` dentro de `tell disk`).

**Problemas encontrados**

- El navegador de partidas solo muestra la versión de protocolo 21; la mayoría de jugadores está en la 24 (D8).
- El upstream v24 entra en conflicto: 27 archivos modificados en los dos lados y 8 con implementaciones distintas de Custom Edition. La fusión se canceló sin cambios.
- Gatekeeper bloquea el app sin firma; las notas del release lo explican.
- El token de `gh` no tenía permiso para borrar paquetes; se borró desde el navegador del propietario.

**Próximas acciones:** que el propietario decida sobre Custom Edition (D13) para poder sincronizar con el upstream.

## 2026-10-10 · Memoria del proyecto

**Implementado**

- Creación de `CLAUDE.md` y de `docs/`: `architecture.md`, `decisions.md`, `tasks.md`, `roadmap.md` y este registro.

**Corregido**

- `docs/macos-port-audit.md` mencionaba un binario `halo-ce-macos-spike` que ya no existe con ese nombre; se corrige en esta sesión.

**Problemas encontrados**

- Ninguno nuevo. La documentación previa no tenía un punto de entrada común; `CLAUDE.md` lo aporta.

**Próximas acciones:** las de `docs/tasks.md`, sección "Próximos pasos recomendados".

## 2026-10-10 · Sincronización con el upstream v25 y relay de FulGerNet

**Implementado**

- Versión del app: 0.2.0 (`CFBundleVersion` 3).
- Fusión con `cybersecurity/halo-ce-universal` (protocolo de red 25) en la rama `sync-upstream-v24`, sin commit. Los 32 archivos en conflicto están resueltos y marcados en el índice.
- Invitado de macOS: shims de las 17 funciones de SDL que faltaban (`port/macos/guest/guest_sdl_desktop.c`). Un único import nuevo del host: `host_sdl_set_error`.
- Host de macOS: 12 funciones `host_*` del upstream que faltaban (`port/macos/host/host_sdl.c`, `host_gl.c`, `host_memory.c`, `host_touch.c` nuevo). Los streams de audio ahora liberan su binding al destruirse, como en Android.
- Relay de FulGerNet portado a `port/linux/src/p2p.c`, `p2p_signal.c` y `p2p_internal.h`, más la opción `network.relay_fallback` (`HALO_NET_RELAY`). Se portaron solo los cambios del relay; el borrado de `HALO_PROFILE` y el estado de error de unión del fork no se tomaron.

**Probado**

- `ninja macos_app` y `ninja macos_dmg` sin errores. El DMG se construyó con Python 3.14 de Homebrew (ver problemas).
- `pytest tools/test_macos_port.py` con `HALO_MACOS_TEST_DATA`: 22 pasan, antes y después del relay.
- El navegador de partidas lista servidores públicos (35–39 servidores, 96–118 jugadores).
- Unirse a una partida pública: la lobby se abre, la conexión es directa por UDP y el jugador entra a la partida. Sin errores nuevos en los logs.
- Build de Linux: todos los pasos de compilación terminan sin error; falla el enlazado de SDL porque falta `cmake`.

**No probado**

- Relay de verdad: la prueba se hizo con conexión UDP directa, así que el relay no llegó a usarse. Para probarlo hace falta una red con NAT estricto o un segundo equipo.
- Android, Windows y el resto de plataformas: no compiladas aquí.
- Teclado, ratón y mandos dentro de la partida tras la sincronización.

**Problemas encontrados**

- `ninja linux` falla en macOS por `-march=native` (se traduce a `apple-m2`, que clang de i686 no conoce). Con `python3 configure.py --portable` la compilación llega hasta el enlazado de SDL.
- Falta `cmake` para el enlazado de SDL del target de Linux. No se ha instalado.
- El Python que usa ninja (`/Applications/Xcode.app/.../python3`) no tiene Pillow, y `tools/macos_dmg.py` lo necesita. Se generó el DMG con `/opt/homebrew/bin/python3.14`.
- Al entrar en el navegador con un perfil nuevo aparece `event handler 'new game if no plyr profiles' failed`; se resuelve copiando `saves/` y `gamestate.txt` de la carpeta de usuario.
- El icono de ventana no se decodifica en esta build: `cannot set the window's icon` en cada arranque. El icono de la app viene del bundle.
- Al arrancar aparecen `no DirectSound for bink` y `failed to open bink file ''`. No se han investigado.

**Próximas acciones:** las de `docs/tasks.md`.
