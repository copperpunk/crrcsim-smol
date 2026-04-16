# Build crrcsim
build:
    @make -s

# Build and run crrcsim
run: build
    ./crrcsim

run-hand: build
    CRRCSIM_LAUNCH_MODE=hand CRRCSIM_HAND_LAUNCH_VEL_MPS=10 ./crrcsim

# Build and run under gdb
debug: build
    gdb -ex run -ex bt -ex quit ./crrcsim

# Fly with joystick passthrough (no flight controller needed)
joystick:
    cd tools && ~/.venvs/rcsim/bin/python -m joystick_passthrough.main
