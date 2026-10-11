# Hoja de ruta

Estado: 2026-10-10. Lo pendiente y su prioridad está en `docs/tasks.md`; aquí,
solo los objetivos por horizonte.

## Corto plazo (próximas semanas)

- **Partidas públicas funcionando entre plataformas:** sincronizar el fork con
  el protocolo de red 24 del upstream. Es lo que más le importa al usuario.
- **Verificación real de macOS:** audio, mandos y partida en red con otra máquina.
- **No romper Linux, Windows ni Android** al sincronizar. Cada cambio compartido
  se comprueba con `unifdef` y, cuando sea posible, con su build.

## Medio plazo (1 a 3 meses)

- **Distribución firmada:** firma Developer ID y notarización del app y del DMG,
  con el flujo de `docs/macos-release.md`. Eliminaría el aviso de Gatekeeper.
- **Compatibilidad de hardware:** probar en más modelos de Apple Silicon y en
  más versiones de macOS, y documentar el resultado.
- **Mantenimiento del release:** cada cambio visible sube la versión (D10) y
  publica un release con sus notas.

## Largo plazo

- **Actualizaciones automáticas en macOS:** el port de Linux tiene un actualizador
  propio; en macOS no existe. Requiere decidir si se usa Sparkle u otra solución,
  y firmar las versiones.
- **Interfaz de configuración gráfica:** los ajustes se editan hoy en `config.toml`
  o en los menús del juego. Una pantalla propia del app sería una mejora.
- **Mantener el fork alineado con el upstream**, para no acumular divergencias
  como la de la versión de red.
