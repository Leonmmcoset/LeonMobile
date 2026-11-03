.PHONY: all configure build test image run release clean legal-info
JOBS ?= 4
all: build
configure:
	cmake -S . -B build/host -G Ninja -DLEON_COMPOSITOR=OFF
build: configure
	cmake --build build/host -j$(JOBS)
test: build
	ctest --test-dir build/host --output-on-failure
	python3 -m unittest discover -s tests -v
image:
	./scripts/build.sh
run:
	./scripts/run-qemu.sh
release:
	./scripts/release.sh
legal-info:
	./scripts/build.sh legal-info
clean:
	cmake -E remove_directory build/host
