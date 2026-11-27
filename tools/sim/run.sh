#!/bin/sh
# Screen simulator: builds LVGL and the real AmuletOS screens for the PC (gcc, fake hardware
# in stubs.c), runs the scenes of sim_main.c and writes tools/sim/out/*.png plus
# out/layout-report.md (text outside the circle). Needs one `idf.py build` first (for
# build/config/sdkconfig.h), tools/fetch_deps.sh and Python with Pillow.
set -e
cd "$(dirname "$0")"
R=../..
M=$R/main
LV=$R/components/lvgl
CFLAGS="-w -O1 -I$LV -I$LV/src -Iinc -DLV_CONF_KCONFIG_EXTERNAL_INCLUDE=\"sdkconfig_host.h\""
mkdir -p obj out
if [ ! -f obj/liblvgl.a ]; then
    for f in $(find $LV/src -name '*.c'); do
        o=obj/$(echo "$f" | sed "s|$LV/src/||; s|/|_|g; s|\.c$|.o|")
        gcc $CFLAGS -c "$f" -o "$o" &
    done
    wait
    ar rcs obj/liblvgl.a obj/*.o
fi
APPS="dice home gallery upload clock info quick settings_app keyboard wifi ota_app calendar"
SRC="$M/ui.c $M/menu.c $M/i18n.c $M/core.c $M/power.c $M/fonts/*.c"
for a in $APPS; do SRC="$SRC $M/apps/$a.c"; done
gcc $CFLAGS -I$M -I$M/apps $SRC stubs.c sim_main.c obj/liblvgl.a -lm -o obj/sim
rm -f out/*.ppm out/*.png
./obj/sim
python3 - <<'PY'
from PIL import Image, ImageDraw
import glob, os
for f in sorted(glob.glob('out/*.ppm')):
    im = Image.open(f).convert('RGB')
    mask = Image.new('L', im.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, im.size[0] - 1, im.size[1] - 1), fill=255)
    bg = Image.new('RGB', im.size, (52, 52, 56))   # outside the circle there is no screen
    bg.paste(im, (0, 0), mask)
    ImageDraw.Draw(bg).ellipse((0, 0, im.size[0] - 1, im.size[1] - 1), outline=(96, 96, 104), width=2)
    bg.save(f[:-4] + '.png')
    os.remove(f)
PY
echo "screenshots in tools/sim/out"
