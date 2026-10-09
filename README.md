# Halo: Combat Evolved para macOS

Halo nativo para Mac con chip Apple (arm64), sin emulador ni máquina virtual.
Es un port no oficial de la comunidad, a partir del código de la
decompilación del juego. No es un producto de Microsoft, Bungie ni Halo
Studios.

> **Necesitas tu propia imagen de disco** de Halo: Combat Evolved de Xbox
> (`.iso` o `.xiso`). Los datos del juego no vienen en la app.

---

## Instalar

1. Abre `Halo-CE-macOS-<versión>-arm64.dmg`.
2. Arrastra **Halo CE** a **Aplicaciones**.
3. Abre la app. La primera vez puede aparecer un aviso de seguridad: la
   app no está firmada con un certificado de Apple Developer ni notarizada.
   Ve a *Ajustes del Sistema > Privacidad y seguridad* y pulsa **Abrir
   igualmente**.
4. El juego te pide la imagen de disco. Selecciónala y espera a que extraiga
   la carpeta `maps` (unos 2 GB). No vuelve a preguntar.

Si macOS dice que la app está dañada, ejecuta en Terminal:

```bash
xattr -dr com.apple.quarantine "/Applications/Halo CE.app"
```

---

## Controles

Los controles son los de teclado y ratón por defecto. Cada acción admite
hasta dos teclas o botones. Puedes cambiarlos en el juego, en
*Settings > Controls Setup*, o en el archivo `config.toml` de la carpeta del
juego (ver [Dónde se guardan los datos](#dónde-se-guardan-los-datos)).

### Movimiento y combate

| Acción | Tecla o botón | Para qué sirve |
| --- | --- | --- |
| **Moverte** | `W` `A` `S` `D` | Avanzar, retroceder y laterales. |
| **Apuntar** | Ratón | Mueve la mira. Es la vista directa. |
| **Disparar** | Botón izquierdo | Usa el arma que tengas en la mano. |
| **Granada** | Botón derecho, `G` | Lanza la granada que tengas seleccionada. |
| **Saltar** | `Espacio` | Salta. También salta una cinemática. |
| **Agacharte** | `Control` izquierdo, `C` | Te agachas para cubrirte. |
| **Cuerpo a cuerpo** | `F`, botón 4 del ratón | Golpe cuerpo a cuerpo. |
| **Recargar** | `R` | Recarga el arma. |
| **Zoom** | `Z`, botón central | Acerca la mira con el arma que tengas. |

### Objetos y vehículos

| Acción | Tecla o botón | Para qué sirve |
| --- | --- | --- |
| **Acción** | `E` | Recoger, entrar o salir de un vehículo. Mantenla para cambiar de arma del suelo. |
| **Cambiar de arma** | Rueda del ratón, `1` | Cambia de arma. Una vuelta de la rueda, un cambio. |
| **Cambiar de granada** | `X` | Alterna entre tipos de granada. |
| **Linterna** | `Q` | Enciende o apaga la linterna. |

### Pantalla y menús

| Acción | Tecla | Para qué sirve |
| --- | --- | --- |
| **Marcador** | `Tab` (mantener) | Muestra las puntuaciones mientras la mantienes. |
| **Menú de pausa** | `Escape` | Pausa la partida o vuelve atrás en los menús. |
| **Liberar o capturar el ratón** | `F12` | Suelta el ratón para usar otras ventanas, y lo vuelve a capturar. |
| **Pantalla completa o ventana** | `F11` | Alterna entre pantalla completa y ventana. |
| **Consola de desarrollo** | `` ` `` | Para pruebas y depuración. |
| **Salir** | `⌘Q`, o *Halo CE > Quit Halo CE* | Cierra el juego. |

> **Teclas `F11` y `F12` en un Mac.** Por defecto, en los teclados de Mac las
> teclas F hacen funciones del sistema (brillo, volumen). Para usarlas como
> F11 y F12, mantén pulsada `Fn` a la vez, o activa *Ajustes del Sistema >
> Teclado > Usar las teclas F1, F2, etc. como teclas de función estándar*.

### En los menús

| Acción | Entrada |
| --- | --- |
| Moverse por las opciones | Flechas, o `W` `A` `S` `D` |
| Elegir | `Espacio` o `Intro`, o clic izquierdo |
| Volver atrás | `Escape`, `Retroceso` o clic derecho |
| Cambiar un valor | Clic en la mitad izquierda o derecha del ajuste |

El puntero del ratón señala la opción que tiene debajo, y esa opción queda
seleccionada. Un botón que mantengas pulsado desde un menú no dispara hasta
que lo sueltes y vuelvas a pulsarlo.

### Mandos

El juego reconoce los mandos a través de SDL3: el primero conectado controla
al jugador 1, y los demás a los jugadores 2 a 4. Los mandos **no se han
probado** en macOS todavía.

---

## Recomendaciones de hardware

Esto es lo que he medido y lo que no.

**Probado:** MacBook Pro de 14" con Apple M2 Pro (12 núcleos: 8 de rendimiento
y 4 de eficiencia), 16 GB de memoria, macOS 27.0.1, pantalla Retina.

- Nivel *a30* de la campaña en ventana de 1280×960 (pantalla de 2560×1920
  a escala Retina), sin sincronización vertical: unos 225 fotogramas por
  segundo de media a lo largo de 60 segundos.
- Memoria del proceso en un nivel: unos **407 MB** de memoria residente y
  **710 MB** de pico. En el menú principal, unos 285 MB.
- En disco: unos **2 GB** para la carpeta `maps`, además de la app.

**Recomendación:**

- **Cualquier Mac con chip Apple** (M1 o posterior) con macOS 27. El juego
  solo se compila para Apple Silicon: los Mac con procesador Intel no lo
  ejecutan.
- **16 GB de memoria** es lo que he probado. Con 8 GB el juego debería
  funcionar, porque en un nivel usa menos de 1 GB, pero no lo he probado.
- **Espacio libre:** deja al menos 3 GB para los datos y las partidas.

**No verificado:** M1, M3 y M4, MacBook Air, Mac mini y Mac Studio. Su
rendimiento puede ser distinto. Si lo pruebas en uno de ellos, dime el
modelo y el resultado, y lo añado aquí.

---

## Dónde se guardan los datos

Todo va en `~/Library/Application Support/Halo CE`. Nada se escribe dentro de
la app.

| Archivo o carpeta | Contenido |
| --- | --- |
| `maps/` | Los mapas extraídos de tu imagen de disco. |
| `saves/` | Tus partidas guardadas y tus perfiles. |
| `config.toml` | Los ajustes: gráficos, controles y sonido. |
| `debug.txt` | El registro del juego. Útil si algo falla. |
| `macos-host.log` | El registro de la app. |

Para hacer copia de seguridad, copia la carpeta `saves/`.

---

## Limitaciones conocidas

- **Firma:** la app no está firmada con Developer ID ni notarizada.
  Gatekeeper la bloquea hasta que la permitas (ver [Instalar](#instalar)).
- **Vídeos:** las cinemáticas de vídeo (Bink) no están disponibles, así que
  se saltan.
- **Audio, mandos y juego en red:** no se han probado a fondo en macOS.
- **Solo Apple Silicon:** no hay versión para Intel.

---

## Problemas

Si algo falla, abre un *issue* en el repositorio y adjunta `debug.txt` y
`macos-host.log` de la carpeta de datos, después de revisarlos por si tienen
algo personal.

---

## Créditos

Este port parte de la decompilación de Halo: Combat Evolved de
[punpckhdq/halo](https://github.com/punpckhdq/halo), de
[bnunu/halo-1](https://github.com/bnunu/halo-1) y de
[cybersecurity/halo-ce-universal](https://github.com/cybersecurity/halo-ce-universal).
La técnica de arena para el invitado proviene del port de iOS de
[Nicholas Dominici](https://github.com/NicholasDominici/halo-ce-ios). El
proyecto se publica bajo CC0 1.0 (`LICENSE.md`).
