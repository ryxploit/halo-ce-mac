# Controles en macOS

El port de macOS usa los controles de escritorio que ya existen en la capa de
plataforma compartida con Linux y Windows (`port/linux/src/sdl_platform.c`,
`port/linux/src/xinput_sdl.c`; la referencia completa está en
`port/linux/README.md`, "Controls"). No hay controles táctiles. Las acciones
son las del juego; no se han añadido acciones nuevas.

## Teclado y ratón (por defecto)

El teclado y el ratón controlan al jugador del mando 1. Cada acción admite
hasta dos teclas o botones.

| Acción | Teclas y botones |
| --- | --- |
| moverse | W, A, S, D |
| apuntar | ratón |
| disparar | botón izquierdo |
| lanzar granada | botón derecho, G |
| saltar (y saltar una cinemática) | espacio |
| agacharse | Control izquierdo, C |
| golpe cuerpo a cuerpo | F, botón 4 del ratón |
| recargar | R |
| acción (recoger; mantener para cambiar de arma del suelo; subir o bajar de un vehículo) | E |
| cambiar de arma | rueda del ratón, 1 |
| cambiar de granada | X |
| linterna | Q |
| zoom | Z, botón central |
| marcador (mantener) | Tab |
| menú de pausa | Escape |

Siempre:

| Tecla | Efecto |
| --- | --- |
| F12 | libera o vuelve a capturar el ratón |
| F11 | alterna entre pantalla completa y ventana |
| \` | consola de desarrollo |
| ⌘Q, o el menú *Halo CE > Quit Halo CE* | salir |

En la mayoría de teclados de Mac, F11 y F12 necesitan la tecla Fn, o tener
activado "Usar F1, F2, etc. como teclas de función estándar" en los ajustes
de teclado de macOS. Sin eso, F11 muestra el escritorio.

En los menús:

- El puntero da el foco al elemento que tiene debajo.
- El clic izquierdo selecciona y el clic derecho vuelve atrás.
- La rueda recorre los elementos.
- Las flechas (y WASD) mueven la selección, espacio o Intro seleccionan, y
  Escape o Retroceso vuelven atrás.

## Reasignar

En el juego: *Settings > Controls Setup* del perfil. Fuera del juego: la
sección `[controls]` de `config.toml`, en
`~/Library/Application Support/Halo CE/config.toml`. Los nombres de las
teclas son los de SDL; el archivo describe cada ajuste.

## Captura del ratón y foco

Durante el juego, el ratón queda capturado en modo relativo. Se libera:

- con F12, que también lo vuelve a capturar;
- en los menús, donde se muestra el puntero;
- al perder la ventana el foco (⌘Tab, clic en otra app): el juego suelta las
  teclas y botones que estuvieran pulsados, y SDL deja de capturar el ratón.

Al recuperar el foco, el ratón se captura de nuevo, salvo que se hubiera
liberado con F12.

## Mandos

SDL3 añade el primer mando conectado al jugador del mando 1, y los demás a
los mandos 2 a 4, como en Linux y Windows. El esquema de botones de cada
perfil está en *Settings > Gamepads*.

## Qué se ha comprobado

- Las flechas, Intro y Escape mueven la selección, eligen y vuelven atrás en
  el menú principal.
- El puntero da el foco a SETTINGS y el clic izquierdo abre la pantalla de
  CAMPAIGN.
- Salir desde el menú *Quit Halo CE* cierra el juego con código 0. El atajo
  ⌘Q de ese mismo menú no se pulsó en la prueba.
- Sin foco, el juego descarta el teclado; es lo previsto (`FOCUS_LOST` en
  `sdl_platform.c`).

No comprobado en macOS: las teclas dentro de una partida (disparo, salto,
etc.), F11/F12, la reasignación y los mandos. Su código es el mismo que en
Linux y Windows y no tiene cambios para macOS.

Conocido: en el menú principal sin perfil de jugador, cancelar la pantalla
de nombre del perfil muestra `event handler 'new campaign decision' failed`.
Es el comportamiento del motor (`new_campaign_decision` devuelve `FALSE` si
el teclado virtual se cerró sin texto,
`source/interface/ui_widget_event_handler_functions.c`), no del port.
