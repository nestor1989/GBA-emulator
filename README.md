# Play Again

Emulador de GBA para Android. Abre una ROM (suelta, `.zip` o `.7z`), corre a velocidad completa con gamepad táctil, guarda el `.sav` solo y tiene 1 save state manual. Además: turbo ×2/×4/×8, trucos (GameShark, Action Replay, CodeBreaker) e importar/exportar partidas de otros emuladores. Todo gratis; el único pago es el Pack Colaborador (cosmético) y propinas, por Google Play. Viene con **Cometa**, un juego propio (`homebrew/cometa`), y está en inglés, español, portugués (Brasil), francés, alemán e italiano.

## Compilar

1. Android Studio **Panda 2 (2025.3.2) o superior** (AGP 9.1.1, Gradle 9.3.1). Si tu Studio es más viejo, avisá y bajo AGP.
2. Submódulos (mGBA y Oboe, fijados a un commit):
   ```
   git submodule update --init --depth 1
   ```
3. Abrir la carpeta en Studio. Si faltan **NDK 28.2.13676358** o **CMake 3.22.1**, Studio los instala (o SDK Manager → SDK Tools).
4. Run `app` (variante debug). Logs nativos: `adb logcat -s emucore`.

El botón «Jugar Cometa» del inicio abre el juego incluido (`app/src/main/assets/games/cometa.gba`). El build release falla si encuentra cualquier otra ROM en assets. La carpeta `app/src/debug/assets/demo/` (Butano Fighter) ya no se usa: se puede borrar.

El código nativo se compila con `-O2` también en debug: a `-O0` mGBA no llega a 60 fps en gama baja.

## Estructura

```
app/    UI: HomeActivity (abrir/continuar/Cometa), GameActivity, GamepadView, RomStore (SAF + copias locales),
        SaveFolder (carpeta de partidas)
        Support.kt (Play Billing), Cosmetics.kt (temas e íconos), SupportActivity («Apoya el proyecto»)
core/   Librería con NDK: Emulator.kt / NativeBridge.kt + src/main/cpp
        emu_core.c   loop de emulación, sincronía por audio, triple buffer, .sav y save state
        audio_oboe.cpp, video_gles.c, jni_bridge.c
third_party/mgba   submódulo (MPL 2.0)
third_party/oboe   submódulo 1.10.0 (Apache 2.0), compilado desde fuente
homebrew/cometa    el juego incluido: código, arte y música propios (ver su README)
fastlane/          ficha de Play en 7 idiomas: textos, capturas, gráfico destacado (ver store/README.md)
store/             productos de compra por idioma y el generador de capturas
```

Idiomas de la app: `values/` es inglés (el respaldo para cualquier idioma sin carpeta propia); `values-es` (neutro, sirve para Latinoamérica y España), `values-pt-rBR`, `values-fr`, `values-de`, `values-it`. En Android 13+ cada usuario puede elegir el idioma de la app desde los ajustes del sistema.

- Partidas: `files/saves/<CRC32>.sav` y `files/states/<CRC32>.ss1`. Se identifican por el CRC de la ROM, así que renombrar el archivo o pasarlo a zip no pierde la partida. Ver «Dónde quedan las partidas».
- La ROM abierta se copia a `no_backup/roms/` (nunca va al backup de Google). Solo se guarda la última.
- Trucos: `files/cheats/<CRC32>.json`. Al importar una partida, la anterior queda en `files/saves/backup/<CRC32>.sav` (menú → «Volver a la partida anterior»; hacerlo dos veces lo deshace).

## Dónde quedan las partidas

El emulador siempre juega sobre su copia privada (`files/saves/<CRC32>.sav`). Esa copia se pierde al desinstalar, salvo que Auto Backup la devuelva. Para no depender de eso hay una **carpeta de partidas** opcional (`SaveFolder.kt`):

- El jugador elige una carpeta suya (lo ideal: la de los juegos). Cada partida se copia ahí como `<nombre del archivo del juego>.sav`, el mismo nombre que usan My Boy!, VBA y la mayoría, así que sirve para pasar partidas en los dos sentidos.
- Se ofrece desde el inicio después de cerrar el primer juego propio (no antes de jugar, para no frenar el arranque; como mucho 2 veces), y siempre desde Menú → «Carpeta de partidas» (elegir, cambiar o dejar de usarla).
- Al reinstalar, si la app recupera sus preferencias del backup, el inicio pide una vez volver a elegir la carpeta (Android no devuelve el permiso). Si no, se elige desde el menú de cualquier juego.
- Sincronía por juego, con un CRC de lo que tenía cada lado la última vez que coincidieron:
  - al abrir un juego: si la app no tiene partida, o solo cambió la de la carpeta (se jugó en otro emulador), carga la de la carpeta; si solo cambió la de la app, la escribe en la carpeta; si cambiaron las dos, pregunta cuál usar y la otra queda en «Volver a la partida anterior»;
  - al salir o pausar: copia la partida a la carpeta, salvo que la de la carpeta haya cambiado mientras tanto (entonces pregunta al abrir).
- Una partida que es todo `0xFF` o `0x00` cuenta como vacía: nunca pisa a una con progreso.
- Al elegir la carpeta se copian las partidas de los juegos ya jugados (solo las que no chocan con una distinta que ya esté ahí).

Auto Backup (`res/xml/backup_rules.xml` hasta Android 11, `data_extraction_rules.xml` desde Android 12) guarda partidas, trucos y preferencias, y deja afuera:

- los estados rápidos (`files/states/`): pesan mucho, y si el backup pasa el límite de 25 MB Google deja de guardar todo, partidas incluidas. En una transferencia directa a otro teléfono (sin límite) sí viajan;
- `save_folder_prompts`, para que una reinstalación vuelva a ofrecer la carpeta.

Las ROMs nunca entran: están en `no_backup/`.

## Partidas de otros emuladores

Menú → «Importar partida de otro emulador». Primero una pantalla le dice al jugador qué archivo buscar («<nombre del juego>.sav», o `.srm` de RetroArch) y el selector se abre en la carpeta de donde abrió el juego, que es donde My Boy! y VBA dejan la partida. Si el archivo elegido no se llama como el juego, la confirmación lo advierte. Acepta partidas de batería, no estados guardados:

- `.sav` crudo: VBA, VBA-M, My Boy!, mGBA y la mayoría de los emuladores.
- `.srm` de RetroArch/Lemuroid (core mGBA): viene rellenado a 128 KB; se recorta al tamaño del cartucho.
- `.gsv` (GameShark SP) y `.sps` (SharkPort): solo se aceptan si son de este mismo juego.

Los estados guardados (`.ss0`, `.state`) no se transfieren entre emuladores. Al migrar, el jugador tiene que guardar dentro del juego antes de pasarse. «Exportar partida» genera un `.sav` crudo que abren los otros emuladores.

Limitación: en Android 11+ las carpetas privadas de otras apps (`Android/data/...`) no se pueden abrir con el selector. Si el otro emulador guarda ahí (pasa con RetroArch de Play Store y Lemuroid), el jugador primero tiene que exportar o copiar la partida desde esa app.

## Pack Colaborador y propinas

Todas las funciones son gratis. Lo pago es cosmético y se cobra solo con Google Play Billing 9.1.0 (Google exige la 8 o superior desde el 31/08/2026). Sin publicidad.

| ID en Play Console | Tipo | Precio sugerido (USD) | Qué da |
|---|---|---|---|
| `supporter_pack` | Producto único, no consumible | 2,99 | 6 temas de controles, 2 íconos extra, insignia «♥ Colaborador» |
| `tip_small` | Producto único, consumible | 0,99 | Nada (propina) |
| `tip_medium` | Producto único, consumible | 2,99 | Nada (propina) |
| `tip_large` | Producto único, consumible | 4,99 | Nada (propina) |

En Play Console: Monetizar → Productos → Productos únicos. Crear los 4 con esos IDs exactos (no se pueden cambiar después) y dejar que Play calcule los precios locales; revisá ARS y BRL a mano. En Play Console no hay casilla de «consumible»: lo decide la app, que consume las propinas apenas llegan.

Cómo se comporta:
- La compra se valida en el teléfono (no hay servidor). El pack se reconoce (acknowledge) al recibirlo; si no, Google lo reembolsa a los 3 días. Las propinas se consumen para poder repetirlas.
- Cada vez que se abre el inicio se consultan las compras: restaura el pack al reinstalar, activa pagos pendientes (efectivo, transferencia) y, si hubo reembolso, vuelve a Clásico (tema e ícono).
- El ícono se cambia con `activity-alias` (`LauncherClassic`, `LauncherNight`, `LauncherCoral`). Algunos launchers quitan el acceso directo de la pantalla de inicio al cambiarlo; la app lo avisa antes.
- Ningún botón de pago aparece en partida. No hay links a PayPal, Cafecito ni Mercado Pago dentro de la app ni en la ficha: la política de pagos de Play lo prohíbe (solo exceptúa donaciones a ONG). Esos links van en la web, GitHub o redes.

Probar:
- **Sin Play Console (ya):** en debug, «Apoya el proyecto» tiene abajo «[Debug] Desbloquear el pack sin pagar». Sirve para probar temas, íconos e insignia. No existe en release.
- **Compras reales:** la variante debug usa el applicationId `com.nestordelrio.emu.debug`, que Play no conoce: ahí los productos figuran «No disponible por ahora». Para probar el cobro: build release firmado → pista de prueba interna → tu cuenta como tester de licencias (Configuración → Pruebas de licencias). Los testers pagan con tarjetas de prueba y no se cobra nada.
- Si Android Studio dice que la actividad está deshabilitada al hacer Run, es porque elegiste otro ícono: volvé a Clásico o desinstalá la app.

Antes de publicar:
- Play Console → Configuración → Perfil de pagos: cuenta bancaria para recibir USD por transferencia (no PayPal). Google paga desde US$100 acumulados.
- Anotarse en el nivel de comisión del 15% (no es automático; sin eso es 30%).
- Seguridad de los datos: la app ahora incluye la biblioteca de facturación de Google Play; revisá el formulario. El manifest quita los permisos de ubicación que trae como dependencia (`tools:node="remove"`): revisá los permisos finales en la pestaña Merged Manifest (lo esperable: `BILLING`, `INTERNET` y `ACCESS_NETWORK_STATE`).

## Cierres en Play Console

El build release mete los símbolos de depuración del código nativo (mGBA, JNI, audio) dentro del AAB (`ndk.debugSymbolLevel = "FULL"` en `app/build.gradle.kts`; el código nativo se compila con `-g` también en release). Al subir el AAB, Play Console los toma solo y en Android vitals → Crashes and ANRs los cierres nativos se ven con nombre de función, archivo y línea. No cambian lo que descarga el usuario.

- Al correr `bundleRelease`, no debería aparecer «Unable to extract native debug metadata». Si aparece, algo llegó sin símbolos.
- Si alguna vez subís un APK en lugar del AAB: el archivo está en `app/build/outputs/native-debug-symbols/release/native-debug-symbols.zip` y se sube a mano en App bundle explorer → la versión → Downloads.
- Kotlin no necesita nada: R8 está apagado, así que los stacks de Java/Kotlin ya se leen. Si algún día se activa, el archivo de mapeo también viaja solo en el AAB.

## Test nativo sin dispositivo (Mac o Linux)

```
cmake -S core/src/main/cpp -B build-host && cmake --build build-host
./build-host/emu_host_test juego.zip juego.7z '!zip-sin-rom.zip'
```

Genera una ROM sintética y verifica: detección por contenido, .zip/.7z, ritmo a ~60 fps por reloj y por audio, video, `.sav` escrito, save state, turbo, trucos e importar/exportar partidas.

## Actualizar mGBA

```
git -C third_party/mgba fetch --depth 1 origin <commit>
git -C third_party/mgba checkout <commit>
git add third_party/mgba
```

La pantalla de licencias toma el commit del submódulo en cada build: el link al código fuente nunca queda desactualizado. Si alguna vez hay que modificar un archivo de mGBA, va a un fork público y el submódulo apunta al fork.

## QA fase 2

- [ ] Alguien que no conoce la app: abrirla y estar jugando en menos de 5 s, sin instrucciones.
- [ ] Cruceta con diagonales y deslizando el pulgar; deslizar de B a A; A+B tocando entre los dos; L, R, Start, Select.
- [ ] 5 min sin tirones ni crujidos. Conectar y desconectar auriculares en partida.
- [ ] Guardar dentro del juego, Menú → Salir, volver: la partida sigue. Repetir cerrando la app desde Recientes.
- [ ] Menú → Guardar estado, jugar un poco, Menú → Cargar estado.
- [ ] «Abrir juego» con: `.gba` suelto, `.zip`, `.7z`, zip sin juego (mensaje claro), `.rar` (mensaje claro).
- [ ] El selector no muestra fotos, videos ni documentos. «Ver todos los archivos» muestra todo.
- [ ] Vertical: el juego ocupa todo el ancho, nítido (sin píxeles desparejos ni borroso). Barra Turbo / Guardar / Cargar / Menú debajo.
- [ ] Turbo: cada toque pasa a ×2 (con sonido acelerado) → ×4 → ×8 (mudos) → apagado. La primera vez que llega a ×4 avisa que el sonido vuelve al apagarlo.
- [ ] Trucos: agregar un código válido (se guarda y se activa), uno inválido (mensaje), apagarlo y borrarlo.
- [ ] Importar un `.sav` de My Boy!/VBA y un `.srm` de RetroArch: el juego se reinicia con esa partida. «Volver a la partida anterior» la deshace.
- [ ] Exportar partida y abrir el `.sav` en otro emulador.
- [ ] Carpeta de partidas: abrir un juego propio, guardar dentro del juego, salir. El inicio ofrece la carpeta y el selector arranca en la carpeta del juego. Elegirla: aparece `<juego>.sav` ahí.
- [ ] Seguir jugando, guardar, salir: el `.sav` de la carpeta se actualiza (fecha).
- [ ] Desinstalar, reinstalar, abrir el mismo juego y elegir la misma carpeta (Menú → «Carpeta de partidas», o el aviso del inicio si aparece): el juego se reinicia con la partida y avisa «Se cargó la partida guardada en …».
- [ ] Copiar a la carpeta un `.sav` distinto del mismo juego (de otro emulador) y abrirlo: si la app no cambió, lo carga solo; si los dos cambiaron, pregunta. Elegir «La de la app» y después Menú → «Volver a la partida anterior»: vuelve la de la carpeta.
- [ ] Menú → «Carpeta de partidas» → «Dejar de usarla»: las partidas ya no se copian.
- [ ] Los estados rápidos no aparecen en el backup: Ajustes → Google → Copia de seguridad, o `adb shell bmgr backupnow <applicationId>` y revisar que `files/states` no esté.
- [ ] Rotar en partida: no se reinicia. Ir a inicio y volver: pausa y sigue.
- [ ] Primer arranque: «Jugar Cometa (incluido)» → pantalla de título → START: jugando en menos de 5 s.
- [ ] Cometa: perder las 3 vidas con un puntaje mayor al récord, salir y volver: el récord quedó guardado.
- [ ] Idioma: cambiar el idioma del teléfono (o el de la app en Ajustes → Apps → Play Again → Idioma, Android 13+) a inglés, portugués, francés, alemán e italiano: menús, diálogos y licencias traducidos, sin textos cortados.
- [ ] Inicio → «♥ Apoya el proyecto»: precios «…» o «No disponible por ahora» en debug, sin cuelgues. Sin Play Store: «Google Play no está disponible».
- [ ] Con «[Debug] Desbloquear»: aparece la insignia en el inicio, se pueden elegir los 7 temas (se ven en vertical y horizontal) y los 3 íconos (el ícono cambia en el launcher).
- [ ] «[Debug] Volver a bloquear»: tema e ícono vuelven a Clásico.
