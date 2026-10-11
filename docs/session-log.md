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
