#!/bin/sh
# Builds build/cometa.gba. Needs arm-none-eabi-gcc (apt: gcc-arm-none-eabi libnewlib-arm-none-eabi)
# and Python 3 with numpy + Pillow. Pass AUTOPILOT=1 for the self-playing capture build.
set -e
cd "$(dirname "$0")"
mkdir -p build
python3 tools/make_assets.py
CFLAGS="-mcpu=arm7tdmi -mthumb -mthumb-interwork -O2 -ffreestanding -fno-strict-aliasing -std=gnu11 -Wall -Wextra -Wno-unused-parameter"
OUT=build/cometa
if [ -n "$AUTOPILOT" ]; then CFLAGS="$CFLAGS -DAUTOPILOT"; OUT=build/cometa-autopilot; fi
if [ -n "$EXTRA_CFLAGS" ]; then CFLAGS="$CFLAGS $EXTRA_CFLAGS"; OUT=build/cometa-test; fi
arm-none-eabi-gcc -mcpu=arm7tdmi -c src/crt0.s -o build/crt0.o
for f in main sound assets; do
    arm-none-eabi-gcc $CFLAGS -c src/$f.c -o build/$f.o
done
arm-none-eabi-gcc -mcpu=arm7tdmi -mthumb -nostartfiles -T src/gba.ld -o $OUT.elf \
    build/crt0.o build/main.o build/sound.o build/assets.o -lc -lgcc -Wl,--no-warn-rwx-segments
arm-none-eabi-objcopy -O binary $OUT.elf $OUT.gba
python3 tools/fix_header.py $OUT.gba
arm-none-eabi-size $OUT.elf
