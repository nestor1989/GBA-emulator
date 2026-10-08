# Emulador GBA — contexto del proyecto

Proyecto iniciado el 2026-10-05. Nombre: **Play Again** (título en Play: «Play Again: Emulador GBA retro» en español, «Play Again: GBA Retro Emulator» en inglés). Elegido por Néstor el 2026-10-06 entre Tap Start, Play Again y Retro Orbit, revisados en Play; reemplaza a «Cartucho», que no le gustaba: quería un nombre en inglés para el ASO.

Dossier completo (mercado, monetización, riesgos, enfoque técnico, ASO y fuentes):
https://claude.ai/code/artifact/4010d0a7-ae23-4982-9144-1201c5e04b0d

## Qué es

Emulador de Game Boy Advance para Android, publicado en Google Play desde la cuenta de Néstor del Río. Público objetivo: jugadores de 25 a 40 años que vuelven a los juegos de su infancia y quieren algo que ande en 5 segundos, sin configurar nada.

Diferenciador: el primer minuto. Abrir la app y estar jugando, sin buscar ROMs, sin configuración y sin un anuncio en el medio.

- Juego incluido: **Cometa**, un shooter espacial propio (código, arte y música originales, en `homebrew/cometa`), para el primer arranque y las capturas. Sin permisos de terceros.
- Español primero (es-419), no traducido del inglés.
- Cero anuncios durante la partida, también en la versión gratis.
- Todas las funciones gratis; se cobra solo un pack cosmético y propinas (ver Monetización).

## Roles

- **Claude:** todo el código, ASO, textos del listing, localización, política de privacidad (publicada como Artifact), categoría y tags de Play Store, assets del listing.
- **Néstor:** aprueba decisiones clave, QA en dispositivo real, compila y firma el AAB, publica, maneja Play Console y cualquier respuesta a revisiones de Google.

## Decisiones tomadas

**Stack**
- App nativa en Kotlin. No aplica el pipeline HTML5/Canvas + WebView de los juegos: un emulador en WebView pierde en latencia y batería.
- Core: mGBA (MPL 2.0) como submódulo git + CMake + JNI propio sobre la API `mCore` (sin libretro). Fijado a un commit de `master` (dfb2e7a, 2026-10-05), no a 0.10.5: la 0.10.x trae blip_buf bajo LGPL 2.1; master lo quitó en abril 2024. Publicar los cambios a archivos de mGBA que se modifiquen (fork público antes del release).
- Nada de código GPL (Lemuroid, VBA-M): obligaría a abrir toda la app.
- BIOS: la BIOS HLE de mGBA. Nunca distribuir la BIOS de Nintendo.
- Acceso a ROMs: Storage Access Framework con permisos persistentes. Nunca MANAGE_EXTERNAL_STORAGE.
- Render OpenGL ES 2 con escalado sharp-bilinear (filtro LCD: gratis, pendiente). Audio con Oboe 1.10.0 compilado desde fuente (submódulo); la emulación se sincroniza por audio.
- Build: AGP 9.1.1 (Kotlin integrado), Gradle 9.3.1, compileSdk/targetSdk 36, minSdk 24, NDK 28.2 (páginas de 16 KB), ABIs arm64-v8a, armeabi-v7a y x86_64. Nativo a -O2 también en debug.
- Fase 2 sin Compose ni AndroidX: solo APIs del framework.
- .zip y .7z los abre mGBA (minizip + LZMA SDK); detecta la ROM por el header, no por la extensión. .rar no soportado (mensaje claro).
- Partidas: `files/saves/<CRC32>.sav` y `files/states/<CRC32>.ss1`. La ROM abierta se copia a `no_backup/roms/`. Auto Backup guarda partidas, trucos y preferencias pero no los estados rápidos (límite de 25 MB).
- Carpeta de partidas opcional (aprobada por Néstor el 2026-10-06, tras perder una partida al reinstalar): cada partida se copia también a una carpeta elegida por el jugador como `<nombre del juego>.sav`, con sincronía en los dos sentidos (también sirve con otros emuladores). Se ofrece después de cerrar el primer juego propio y desde el menú del juego. Detalle en README.md, «Dónde quedan las partidas».
- applicationId `com.nestordelrio.playagain` (final). El namespace del código sigue siendo `com.nestordelrio.emu`.
- Idiomas de la app: `values/` en inglés (respaldo), `values-es` (neutro, sirve para es-419 y es-ES), `values-pt-rBR`, `values-fr`, `values-de`, `values-it`; idioma por app en Android 13+.
- Turbo: cada toque en el botón pasa a ×2 → ×4 → ×8 → apagado (elegido por Néstor el 2026-10-05; antes era una velocidad fija elegida desde el menú).

**Monetización**
- Decidido por Néstor (2026-10-05): todas las funcionalidades son gratis (turbo, trucos, save states, importar/exportar partidas, etc.). Reemplaza el "Pro" de funciones del dossier.
- Modelo elegido (2026-10-05): **Pack Colaborador** (`supporter_pack`, no consumible, USD 2,99 sugerido): 6 temas de controles, 2 íconos de app extra e insignia «♥ Colaborador»; no desbloquea funciones. **Propinas** (`tip_small`/`tip_medium`/`tip_large`, consumibles, USD 0,99/2,99/4,99). Todo por Google Play Billing 9.1.0, validado en el dispositivo (sin servidor).
- Nada de pagos fuera de Play dentro de la app ni en la ficha (PayPal, Cafecito, Mercado Pago): la política de pagos lo prohíbe salvo donaciones a ONG; en julio 2026 Google le hizo quitar el link de donaciones a AnkiDroid. Esos links solo en web, GitHub o redes (Néstor tiene paypal.me/nestordelrio; Cafecito cobra 5% + Mercado Pago, en pesos).
- Cobro: Google paga en USD por transferencia a una cuenta bancaria del perfil de pagos (no a PayPal), desde US$100 acumulados. Comisión 30%, o 15% sobre el primer millón anual anotándose en ese nivel (no es automático). El esquema nuevo de Google (10% el primer millón) llega a Argentina el 30/09/2027.
- Sin AdMob en v1: la cuenta de AdMob es una sola y sirve a todos los juegos del portfolio. Revisar con datos post-lanzamiento.
- Sin suscripciones (nadie en el nicho las usa).

**Reglas duras de producto y listing**
- Sin ROMs comerciales incluidas, sin links a sitios de ROMs, sin descargas de juegos dentro de la app.
- Sin "Nintendo", "Game Boy", "Pokémon" ni nombres de juegos comerciales en título, ícono, descripción o capturas.
- Capturas del listing solo con homebrew.

## Competencia (orden de magnitud, sep 2026)

| App | Modelo | Nota |
| --- | --- | --- |
| My Boy! | Pago USD 4,99 | Líder pago, ~2,4M descargas, mejor link cable |
| John GBAC | Gratis + ads, USD 4,49 quitar ads | 3,7★ |
| RetroArch | Gratis OSS, Plus USD 4,99 | mGBA, complejo de configurar |
| Lemuroid | Gratis OSS (GPL) | mGBA, UI simple |
| GBA.emu | Pago USD 4,99 | 4,4★, menos de USD 5.000/mes estimados |
| Pizza Boy GBA | Basic + Pro | Actualización casi mensual |

Techo realista: más cerca de GBA.emu que de My Boy!.

## Riesgos principales

- Suspensión de la app en Google Play sin explicación (precedente: SuperRetro16).
- Una sanción puede afectar a la cuenta de desarrollador entera, donde también viven Bingo Verificable, Puesto Idle y Colores Sin Trampa.
- Nintendo: hoy apunta a Switch, no a GBA, pero tiene historial agresivo (Yuzu, Ryujinx).
- Licencias open source mal respetadas (precedente: iGBA retirado del App Store).

## Plan

1. **Fase 2 — prototipo:** APK debug que abre un homebrew de licencia libre, corre a velocidad completa con gamepad táctil (cruceta, A, B, L, R, Start, Select), guarda el .sav al salir y tiene 1 save state manual. Listo cuando alguien que no conoce la app está jugando en 5 segundos sin instrucciones.
2. **MVP:** lista de .gba desde carpeta, guardado automático, 3 save states, gamepad Bluetooth.
3. **Extras (gratis):** Drive, filtro LCD. Avance rápido, trucos y Play Billing ya están hechos.
4. **Empaquetado y listing:** ola 1 de localización (en, es-419, es-ES, pt-BR, fr, de, it). App traducida y ficha lista en `fastlane/metadata/android` (ver `store/README.md`).

## Estado

- 2026-10-05: código de la fase 2 entregado en la carpeta GBA-emulator del Mac de Néstor (submódulos agregados, sin commit). Verificado sin Android SDK: test nativo de host (ROM suelta, .zip, .7z, zip sin ROM, 32 MB, ritmo de 60 fps, .sav, save state) en el sandbox y en el Mac; Kotlin compilado contra android.jar 36. Falta el primer build en Android Studio y el QA en dispositivo (checklist en README.md).
- 2026-10-05 (2ª iteración, tras el primer QA de Néstor con un .zip): vertical con el juego a todo el ancho (escalado sharp-bilinear), barra Turbo/Guardar/Cargar/Menú; turbo ×2 (con sonido) ×4 ×8 (mudos); trucos GameShark/Action Replay/CodeBreaker por juego; importar partidas .sav/.srm/.gsv/.sps de otros emuladores con copia de respaldo, y exportar .sav; selector filtrado por MIME con opción «Ver todos los archivos». Todo sin bloqueo Pro por ahora.
- 2026-10-05 (3ª iteración): guía para importar partidas (qué archivo buscar, selector abierto en la carpeta del juego, aviso si el nombre no coincide); en vertical L/R, cruceta y A/B más arriba, SELECT/START donde estaban. Néstor recuperó su partida de My Boy!.
- 2026-10-05 (4ª iteración, monetización): Play Billing 9.1.0 (Support.kt), temas de controles e íconos con activity-alias (Cosmetics.kt), pantalla «Apoya el proyecto» (SupportActivity) y la insignia en el inicio. En debug hay un botón para desbloquear el pack sin pagar y probar los cosméticos. Productos y cómo probarlos: README.md, «Pack Colaborador y propinas».
- 2026-10-05 (5ª iteración): Cometa (juego propio) incluido en la app con el botón «Jugar Cometa»; app traducida a 6 idiomas; ficha de Play en 7 idiomas con 6 capturas, gráfico destacado e ícono 512 (capturas armadas con la interfaz real y cuadros reales de Cometa); turbo cíclico.
- 2026-10-06 (6ª iteración): carpeta de partidas (`SaveFolder.kt`), pregunta cuál usar si las dos partidas cambiaron, aviso para recuperarla después de reinstalar; estados rápidos fuera de Auto Backup (`res/xml/backup_rules.xml` y `data_extraction_rules.xml`). Kotlin compilado con -Werror contra android-36.
- 2026-10-06 (7ª iteración): nombre nuevo **Play Again** y applicationId `com.nestordelrio.playagain`; ficha de Play actualizada en 7 idiomas (títulos con «retro» donde entra, carpeta de partidas en la descripción, gráfico destacado y capturas regeneradas; Cometa ahora dice «2026 PLAY AGAIN»); política de privacidad publicada.
- Homebrew de QA: Butano Fighter (código zlib, música CC BY-NC). Solo QA interno en debug, nunca en release ni capturas.

## Pendiente

- [ ] Decidir cuenta de Play Console: la actual (legacy, sin testing cerrado, pero expone el portfolio) o una nueva (requiere prueba cerrada de 12 testers por 14 días). Ojo: una cuenta nueva solo absorbe las suspensiones de app (strikes); si Google termina una cuenta, termina también las cuentas relacionadas del mismo desarrollador.
- [ ] Medir volumen real de "emulador gba" en es y pt-BR con una herramienta de ASO.
- [ ] Confirmar categoría (Juegos > Arcade, donde figura My Boy!, o Herramientas) y tags oficiales en Play Console.
- [x] Juego incluido: Cometa, propio (reemplaza conseguir permisos de homebrew de terceros).
- [x] Nombre final: Play Again (antes Cartucho).
- [x] applicationId final: `com.nestordelrio.playagain`.
- [ ] Crear fork público de mGBA y apuntar el submódulo ahí antes del release.
- [x] Elegir el modelo de ingresos: Pack Colaborador + propinas por Play Billing.
- [ ] Crear en Play Console los 4 productos (`supporter_pack`, `tip_small`, `tip_medium`, `tip_large`) y probar la compra en la pista interna con un tester de licencias.
- [ ] Cargar la cuenta bancaria en el perfil de pagos y anotarse en el nivel del 15%.
- [ ] Filtro LCD (gratis).

### Checklist de release (2026-10-05)

Código (Claude):
- [x] Juego de primer arranque en release: Cometa.
- [x] Joysticks Bluetooth por ejes (stick analógico): descartado por Néstor; quedan los botones como están.
- [x] Idiomas: en, es, pt-BR, fr, de, it.
- [x] Símbolos nativos para Play Console: `ndk.debugSymbolLevel = "FULL"` en release y `-g` en el build nativo de release (README, «Cierres en Play Console»).
- [ ] Firma de release: upload key + `signingConfig` que lea `keystore.properties` (fuera de git). Néstor lo retoma después (hoy el build release da «configuración incorrecta» por falta de firma).
- [ ] Opcional para v1: biblioteca por carpeta, 3 slots de estado (hoy 1), filtro LCD.

Néstor:
- [x] applicationId final: `com.nestordelrio.playagain` (2026-10-06; irreversible desde el primer upload).
- [ ] Cargar la ficha en Play Console: todo está en `fastlane/metadata/android` (Claude ya la armó en 7 idiomas).
- [x] Política de privacidad: https://claude.ai/artifact/G2R7ujvKJjMBHq5M2yNJRD (2026-10-06). Falta que Néstor la comparta como «cualquiera con el link».
- [ ] Formularios: respuestas listas en `store/README.md`, «Formularios de Play Console» (Seguridad de los datos: no recopila; IARC con violencia de fantasía por Cometa y compras digitales; público 18+; sin anuncios; sin ID de publicidad). Falta cargarlos.
- [ ] Productos de Billing, perfil de pagos, nivel 15%. Al cobrar, Play muestra la dirección completa del desarrollador.
- [ ] AAB firmado con Play App Signing; prueba interna con compras reales; QA en gama baja y Android 7.
- Ya cumplido: targetSdk 36 (obligatorio desde 31/08/2026), páginas de 16 KB, Billing 8+.
- [ ] MVP: biblioteca por carpeta (OPEN_DOCUMENT_TREE) que liste solo juegos y detecte el .sav vecino de otros emuladores.
