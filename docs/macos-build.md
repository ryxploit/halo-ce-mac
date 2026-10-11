# Compilar el port de macOS

El port de macOS ejecuta el motor real en Apple Silicon. Usa el mismo
invitado ILP32 (`arm64_32`) que el port de Android, dentro de una arena de
4 GiB del propio proceso. El diseño y las decisiones están en
`macos-port-audit.md`; los controles, en `macos-controls.md`; la
distribución, en `macos-release.md`.

Todo lo que se describe aquí se ha ejecutado en la máquina de desarrollo
(ver "Verificado"). No se ha probado en otros Macs ni en otras versiones de
macOS.

## Requisitos

| Herramienta | Versión verificada | Para qué |
| --- | --- | --- |
| macOS en Apple Silicon | 27.0.1 (arm64, Apple M2 Pro) | host y pruebas |
| Xcode | 27.0, Apple clang 21.0.0 | invitado `arm64_32`, host, `codesign`, `hdiutil` |
| Python 3 | 3.14 (Homebrew) | `configure.py` y las herramientas |
| ninja | 1.13.2 | build |
| pkgconf (`pkg-config`) | 3.0.7 | localizar SDL3 |
| SDL3 | 3.4.18 | host y cabeceras del invitado |
| LLVM lld (`ld.lld`) | 23.1.3 | enlazar la imagen ELF del invitado |
| Pillow | 12.3.0 | sólo para regenerar el icono |

Con Homebrew:

```sh
brew install ninja pkgconf sdl3 lld
```

`lld` instala `llvm` como dependencia. Para los tests hace falta `pytest`;
por ejemplo, en un entorno virtual:

```sh
python3 -m venv .venv-macos && .venv-macos/bin/pip install pytest
```

Ni `configure.py` ni ninja descargan nada para macOS. Las fuentes de musl
1.2.5 y las cabeceras Khronos de OpenGL ES se descargan una vez, a mano, con
revisiones y SHA-256 fijados, a `build/third_party/`:

```sh
python3 tools/macos_fetch.py
```

Si falta algo, `ninja macos_guest` falla con un mensaje que dice qué falta.

## Configurar y compilar

Desde la raíz del checkout:

```sh
python3 configure.py --release --pgo=off
ninja macos_app
```

`--release` quita las aserciones de depuración, como en el resto de
plataformas. Sin él, el build de depuración se detiene en la primera
aserción que falle y la escribe en `debug.txt`.

La primera compilación tarda en torno a un minuto (unos 1.500 objetos del
invitado). Opciones de `configure.py` para macOS:

| Opción | Uso |
| --- | --- |
| `--macos-bundle-id ID` | identificador del bundle (por defecto `org.opencommunityedition.haloce.macos`, no oficial) |
| `--macos-guest-cc CLANG` | clang con el target `arm64_32` para el invitado (por defecto `clang`) |
| `--macos-lld LD_LLD` | `ld.lld` (por defecto el del `PATH`, o el de Homebrew) |
| `--macos-cc CLANG` | compilador del host (por defecto `clang`) |

`--pgo=off` evita el aviso de los perfiles de PGO: Apple clang 21 no lee los
perfiles de `pgo/`, que necesitan clang 22.

## Targets

| Target | Resultado |
| --- | --- |
| `ninja macos_guest` | `build/macos/halo_guest.elf` y `build/macos/embed/` (la imagen del invitado) |
| `ninja macos_host` | `build/macos/halo-ce` (ejecutable de desarrollo, enlaza la SDL3 de Homebrew) |
| `ninja macos_app` | `build/macos/Halo CE.app` |
| `ninja macos_dmg` | `build/macos/Halo-CE-macOS-<versión>-arm64.dmg` |
| `ninja macos_spike` | `build/macos/macos_spike` (la ventana SDL del primer hito, sin el juego) |
| `ninja macos_lowmem_probe` | `build/macos/lowmem_probe` (por qué no hay memoria baja) |
| `ninja macos_arena_probe` | `build/macos/arena_probe` (la técnica de arena, de extremo a extremo) |
| `ninja macos_hvf_probe` | `build/macos/hvf_probe` (la alternativa con `Hypervisor.framework`) |

## Ejecutar

```sh
open "build/macos/Halo CE.app"
```

La primera vez, el juego no encuentra la carpeta `maps` y lo dice en un
cuadro de diálogo. Si se acepta, pide la imagen de disco de Xbox (`.iso` o
`.xiso`) del jugador y extrae `maps/` (unos 2 GB). Los datos del juego no se
incluyen ni se descargan.

Todo lo que el juego escribe va a `~/Library/Application Support/Halo CE`:
`config.toml`, `maps/`, `saves/`, `debug.txt` (el log del juego) y
`macos-host.log` (el log del host). Nada se escribe dentro del bundle.

Variables útiles:

- `HALO_MACOS_USER_ROOT=<carpeta>`: otra carpeta de usuario (los tests la
  usan).
- `HALO_MACOS_NO_DIALOGS=1`: un error fatal sólo se registra, sin cuadro de
  diálogo.
- Cualquier `HALO_*` de los ajustes (`port/linux/src/port_config.c`) pasa al
  juego: `HALO_EXIT_AFTER=10`, `HALO_DISPLAY_MODE=windowed`,
  `HALO_NO_VSYNC=1`, `HALO_GPU_STATS=1`, `HALO_SCREENSHOT_DIR` con
  `HALO_SCREENSHOT_EVERY`.
- `halo-ce --check` carga la imagen del invitado y sale sin arrancar el
  juego (no necesita pantalla ni datos).

Un `init.txt` con `map_name levels\a30\a30` en la carpeta de usuario carga
ese nivel directamente.

## Tests

```sh
ninja macos_app macos_dmg macos_lowmem_probe macos_arena_probe macos_hvf_probe
python3 -m pytest tools/test_macos_port.py
```

`tools/test_macos_port.py` comprueba:

- el paso de arena (`tools/macos_asm_lift.py`);
- los tres probes;
- que el host carga la imagen y resuelve todos los imports;
- el mensaje cuando faltan los datos;
- el bundle: `Info.plist`, arquitectura arm64, dependencias dinámicas, rpath
  y firma;
- el DMG montado.

Con `HALO_MACOS_TEST_DATA=<carpeta que contiene maps/>` además arranca el
juego hasta el menú y dibuja 25 s del nivel `a30` con capturas del
renderizador. Los datos se usan en solo lectura, mediante un enlace.

## Verificado (2026-10-08)

En macOS 27.0.1, Apple M2 Pro, con el disco PAL `01.01.14.2342` del jugador:

- `python3 configure.py --pgo=off` y `--release --pgo=off`; `ninja macos_app`
  y `ninja macos_dmg` sin errores.
- `pytest tools/test_macos_port.py`: 22 tests pasan, en el build de
  depuración y en el de release, con los datos del juego.
- Interactivo: el diálogo de datos, el selector de ISO y la extracción de
  `maps/`; el menú principal; teclado (flechas, Intro, Escape), ratón (foco al
  pasar, clic), cierre con la ventana y con el menú *Quit Halo CE*.
- Modos de pantalla: ventana, sin bordes y pantalla completa exclusiva, con
  el cambio de escala Retina.
- Nivel `a30` en ventana a 2560×1920 sin vsync: unos 13.600 fotogramas en
  60 s.

No verificado: otros modelos de Mac, otras versiones de macOS, mandos, que
el audio suene (el log no registra errores de audio, pero nadie lo ha
escuchado en una prueba) y el juego en red.

## Problemas conocidos del build (2026-10-10)

- `ninja macos_dmg` usa el Python de Xcode (`build.ninja`, variable `python`), que no trae Pillow. Si falla con `No module named 'PIL'`, genera el DMG con un Python que lo tenga: `/opt/homebrew/bin/python3.14 tools/macos_dmg.py 'build/macos/Halo CE.app' build/macos/Halo-CE-macOS-<versión>-arm64.dmg`. Se verificó con Pillow 12.3.0.
- `ninja linux` falla en macOS con `-march=native` (clang de i686 no conoce `apple-m2`). Con `python3 configure.py --release --pgo=off --portable` llega hasta el enlazado de SDL, que necesita `cmake` (no instalado aquí). Vuelve a configurar sin `--portable` para el build de macOS.
- El icono de ventana no se decodifica en esta build: el log muestra `cannot set the window's icon` en cada arranque. El icono de la app viene del bundle.

## Limpiar

Sólo los resultados del build de macOS:

```sh
rm -rf build/macos
```

Y las descargas de `tools/macos_fetch.py`, si se quieren repetir:

```sh
rm -rf build/third_party/musl-1.2.5 build/third_party/gl_include
```

La carpeta de usuario (`~/Library/Application Support/Halo CE`) contiene los
mapas extraídos y las partidas del jugador; no forma parte del build y no se
borra con esto.
