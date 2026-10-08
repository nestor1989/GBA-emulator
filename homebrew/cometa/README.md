# Cometa

Shooter espacial horizontal que viene incluido en Play Again. Código, gráficos y música originales, hechos para este proyecto: no depende de permisos de terceros y se puede usar en la app, en las capturas y en la ficha de Play.

- Cruceta: mover. A o B (mantener): disparar. START: empezar y pausa.
- 5 tipos de enemigos, cápsulas «P» que mejoran el disparo (simple, doble, abanico), un jefe al final de cada etapa. Cada etapa cambia los colores del fondo y de los enemigos.
- Récord guardado en la batería del cartucho (SRAM): sirve también para probar que el `.sav` se guarda y se importa/exporta.
- Si nadie toca nada en la pantalla de título, a los 10 s arranca una demo que juega sola.

## Compilar

```
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi   # o devkitARM
pip install numpy pillow
./build.sh                       # build/cometa.gba
cp build/cometa.gba ../../app/src/main/assets/games/cometa.gba
```

`tools/make_assets.py` genera todo el arte y las tablas (`src/assets.c`); no hay archivos de imagen sueltos. La paleta de colores es «Sweetie 16» de GrafxKid (libre).

El encabezado del cartucho deja vacía la zona del logo: el juego es para emuladores y no lleva ningún dato de terceros.

## Probar sin teléfono

`tools/capture.c` corre la ROM con mGBA sin ventana, con botones guionados, y guarda cuadros y audio. Se usó para las capturas de la ficha:

```
AUTOPILOT=1 ./build.sh          # build/cometa-autopilot.gba: la nave juega sola
gcc -O2 -I../../third_party/mgba/include -I<build-host>/mgba/include tools/capture.c \
    -o build/capture <build-host>/mgba/libmgba.a -lz -lm -lpthread   # + los -D de mGBA
./build/capture build/cometa-autopilot.gba guion.txt salida/
```

## Derechos

© 2026 Néstor del Río. Todos los derechos reservados.
