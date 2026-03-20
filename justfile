# Build crrcsim
build:
    @make -s

# Build and run crrcsim
run: build
    ./crrcsim

# Build and run under gdb
debug: build
    gdb -ex run -ex bt -ex quit ./crrcsim
