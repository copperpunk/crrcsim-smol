# Build crrcsim
build:
    @make -s

# Build and run crrcsim
run: build
    ./crrcsim

# Build and run under gdb
debug: build
    gdb -ex run -ex bt -ex quit ./crrcsim

# Fly with joystick passthrough (no flight controller needed)
joystick:
    cd tools && ~/.venvs/rcsim/bin/python -m joystick_passthrough.main
