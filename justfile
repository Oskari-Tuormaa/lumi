board := "esp32s3_devkitc/esp32s3/appcpu"

_build board=board *ARGS:
    west build -b {{board}} . {{ARGS}}

build: _build
build-pristine: (_build board "-p always")

run: (_build "native_sim/native/64")
    ./build/zephyr/zephyr.exe --display_zoom_pct=1000

flash: (_build)
    west flash --no-rebuild

clean:
    -rm -rf build

# Initialise the West workspace (if not already done) and fetch all modules
setup:
    #!/usr/bin/env bash
    if [ ! -d ../.west ]; then
        repo=$(basename "$(pwd)")
        others=$(ls -1 ../ | grep -v "^$repo$")
        if [ -n "$others" ]; then
            echo "Warning: the parent directory ($(realpath ..)) will be used as the West workspace,"
            echo "but it is not empty. West will clone modules alongside these existing entries:"
            echo "$others" | sed 's/^/  /'
            echo ""
            echo "The recommended setup is to clone this repository into an otherwise empty directory."
            read -r -p "Continue anyway? [y/N] " response
            [[ "$response" =~ ^[yY]$ ]] || exit 1
        fi
        west init -l .
    fi
    west update -n -o="--depth=1"
