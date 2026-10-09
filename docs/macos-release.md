# Distribuir el port de macOS

## Qué se produce

```sh
python3 configure.py --release --pgo=off
ninja macos_dmg
```

- `build/macos/Halo CE.app`: el app (`tools/macos_bundle.py`).
- `build/macos/Halo-CE-macOS-<versión>-arm64.dmg`: la imagen de disco
  (`tools/macos_dmg.py`). La versión sale de `CFBundleShortVersionString` en
  `port/macos/Info.plist`.

El DMG tiene un volumen llamado "Halo CE" con `Halo CE.app` y un enlace a
`/Applications`. Su script comprueba el app antes de crear la imagen y falla
con el motivo si no existe o está incompleto:

- falta el app;
- falta `Info.plist`, el ejecutable o el icono;
- el ejecutable no es arm64;
- una biblioteca está fuera del bundle y del sistema;
- la firma no es válida.

Después monta la imagen de solo lectura, sin abrir Finder, y comprueba su
contenido. No necesita privilegios de administrador.

Para crear el DMG desde un app concreto:

```sh
python3 tools/macos_dmg.py "build/macos/Halo CE.app" build/macos/mi-imagen.dmg
```

## Contenido del app

```
Halo CE.app/Contents/
    Info.plist              identificador configurable (--macos-bundle-id),
                            versión mínima de macOS = la del binario
    MacOS/halo-ce           el host, con la imagen del juego en su __TEXT firmado
    Frameworks/libSDL3.0.dylib
    Resources/AppIcon.icns  icono propio (tools/macos_icon.py)
    Resources/Licenses/     avisos de licencia de lo que va compilado dentro
```

- Sólo arm64. No se hace universal2: el motor corre como invitado
  `arm64_32` con la técnica de arena, que es específica de AArch64.
- No incluye datos del juego: el jugador aporta su imagen de disco en el
  primer arranque.
- El identificador por defecto, `org.opencommunityedition.haloce.macos`, no
  es de Microsoft ni de Bungie. El `Info.plist` dice que es un port no
  oficial de la comunidad.
- `LSMinimumSystemVersion` es la versión para la que se compiló el binario
  (27.0 con el SDK de Xcode 27). No se ha probado en versiones anteriores.

## Firma y notarización: estado actual

`tools/macos_bundle.py` firma el app **ad hoc** (`codesign --sign -`), sin
identidad. Eso basta para ejecutarlo en el Mac donde se compiló. **El app no
está firmado con un Developer ID ni notarizado.** En otro Mac, Gatekeeper lo
bloqueará por haberse descargado. El jugador puede permitirlo en *Ajustes
del Sistema > Privacidad y seguridad* ("Abrir igualmente"). Este flujo no se
ha probado en otro Mac.

El app no usa el hardened runtime ni entitlements especiales: la técnica de
arena no necesita JIT ni hipervisor. Sí lo necesita el probe opcional
`hvf_probe`, que no forma parte del app.

## Firmar con Developer ID y notarizar (paso del propietario)

Son pasos separados, con las credenciales del propietario de la cuenta de
Apple Developer. **No guardes certificados, contraseñas ni tokens en el
repositorio.** Los comandos son los habituales de Apple. No se han
ejecutado aquí, porque requieren esas credenciales:

1. Firmar de dentro hacia fuera con la identidad "Developer ID Application"
   del llavero, con hardened runtime y sello de tiempo:

   ```sh
   codesign --force --options runtime --timestamp --sign "Developer ID Application: <nombre> (<TEAMID>)" "build/macos/Halo CE.app/Contents/Frameworks/libSDL3.0.dylib"
   codesign --force --options runtime --timestamp --sign "Developer ID Application: <nombre> (<TEAMID>)" "build/macos/Halo CE.app"
   ```

   Con hardened runtime hay que probar el juego de nuevo. La arena aliasa
   código firmado con `vm_remap` y no crea páginas ejecutables nuevas, pero
   esto no se ha verificado con hardened runtime.

2. Crear el DMG con el app ya firmado (`python3 tools/macos_dmg.py`) y
   firmar el DMG con la misma identidad.

3. Notarizar con un perfil de credenciales guardado en el llavero
   (`xcrun notarytool store-credentials`, una vez), y grapar el ticket:

   ```sh
   xcrun notarytool submit build/macos/Halo-CE-macOS-<versión>-arm64.dmg --keychain-profile "<perfil>" --wait
   xcrun stapler staple build/macos/Halo-CE-macOS-<versión>-arm64.dmg
   ```

## Licencias y contenido

- El proyecto es CC0 1.0 (`LICENSE.md`). La técnica de arena, el embebido
  firmado y parte del host vienen del port de iOS de Nicholas Dominici, también
  CC0; cada archivo adaptado lo indica.
- `Resources/Licenses` incluye los avisos de: musl, la musl matemática del
  port, zlib, expat, kcp, monocypher, tomlc17, miniupnpc, extract-xiso, smaa,
  stb, SDL3 y las fuentes Overpass (OFL) y Newtown.
- No se distribuyen mapas, imágenes de disco, vídeos ni música de Halo. El
  icono es un dibujo propio (`tools/macos_icon.py`), sin ilustraciones,
  logotipos ni tipografía del juego.
- No presentes el app como producto oficial de Microsoft, Bungie ni Halo
  Studios.

## Limpiar

```sh
rm -rf "build/macos/Halo CE.app" build/macos/Halo-CE-macOS-*-arm64.dmg
```
