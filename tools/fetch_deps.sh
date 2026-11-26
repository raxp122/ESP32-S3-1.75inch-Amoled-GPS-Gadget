#!/bin/sh
# Downloads the components that are not kept in git (same versions as the CI):
#   LVGL 9.2.2 and Espressif's esp_new_jpeg (SIMD JPEG decoder, from esp-adf-libs).
set -e
cd "$(dirname "$0")/.."
[ -d components/lvgl ] || git clone -b v9.2.2 --depth 1 https://github.com/lvgl/lvgl components/lvgl
if [ ! -d components/esp_new_jpeg ]; then
    tmp=$(mktemp -d)
    git clone --filter=blob:none --sparse https://github.com/espressif/esp-adf-libs "$tmp/adf"
    git -C "$tmp/adf" checkout 7162d32381d6e1f8c8cba2ee5edcbfe82bc964b1
    git -C "$tmp/adf" sparse-checkout set esp_new_jpeg
    cp -r "$tmp/adf/esp_new_jpeg" components/esp_new_jpeg
    rm -rf "$tmp" components/esp_new_jpeg/test_app
fi
echo "components ready"
