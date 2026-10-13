board := "esp32c3_devkitm/esp32c3"

_build board=board *ARGS:
    west build --sysbuild -b {{board}} . {{ARGS}}

build: _build
build-pristine: (_build board "-p always")

build-sim: (_build "native_sim/native/64" "-d build_sim")
run: build-sim
    ./build_sim/lumi/zephyr/zephyr.exe

build-all: build build-sim

flash: (_build)
    west flash --no-rebuild

monitor:
    picocom -b 115200 /dev/serial/by-id/*Silicon_Labs*-if00-port0

clean:
    -rm -rf build
    -rm -rf build_sim

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
