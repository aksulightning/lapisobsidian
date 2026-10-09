CC ?= cc
PYTHON ?= python3
ENGINE_JOBS ?= 2
ENGINE := engine
CPPFLAGS += -Isrc -I$(ENGINE)/src
CFLAGS ?= -O2 -g
WARN := -std=c89 -Wall -Wextra -Werror -Wshadow -Wconversion
CORE := src/LapisProtocol.c src/LapisSession.c src/LapisWorld.c src/LapisGameplay.c src/LapisEntities.c src/LapisEffects.c src/LapisText.c src/LapisSigns.c src/LapisMining.c
IDENTITY := src/LapisIdentity.c build/md5.o build/dec32le.o build/enc32le.o

.PHONY: all test sanitize probe integration survival progression travel native-smoke engine-prepare native terminal windows web web-test package clean
all: test probe
build:
	mkdir -p build
build/%.o: $(ENGINE)/third_party/bearssl/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/protocol-test: tests/protocol.c $(CORE) $(IDENTITY) src/*.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/protocol.c $(CORE) $(IDENTITY) -o $@
build/gameplay-test: tests/gameplay.c $(CORE) src/*.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/gameplay.c $(CORE) -o $@
test: build/protocol-test build/gameplay-test
	./build/protocol-test
	./build/gameplay-test
	$(PYTHON) tests/package.py
sanitize: build/md5.o build/dec32le.o build/enc32le.o | build
	$(CC) $(CPPFLAGS) $(SANITIZER_FLAGS) -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/protocol.c $(CORE) $(IDENTITY) -o build/protocol-sanitize
	./build/protocol-sanitize
	$(CC) $(CPPFLAGS) $(SANITIZER_FLAGS) -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/gameplay.c $(CORE) -o build/gameplay-sanitize
	./build/gameplay-sanitize
build/lapiscube-probe: tools/probe.c tools/survival_scenario.h tools/progression_scenario.h tools/travel_scenario.h $(CORE) $(IDENTITY) src/*.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -Wall -Wextra -Werror -Wshadow -Wconversion tools/probe.c $(CORE) $(IDENTITY) -o $@
probe: build/lapiscube-probe
build/survival-fixture: tests/survival_fixture.c | build
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror -I../../include $< -o $@
survival: probe build/survival-fixture
	$(PYTHON) tests/survival.py
travel: probe build/survival-fixture
	$(PYTHON) tests/travel.py
progression: probe
	$(PYTHON) tests/progression.py
integration: probe
	$(PYTHON) tests/integration.py
native-smoke: terminal
	$(PYTHON) tests/native_smoke.py
engine-prepare:
	$(PYTHON) tools/prepare_engine.py
native:
	$(PYTHON) tools/prepare_engine.py --mode native
	$(MAKE) -C build/engine TARGET=LapisCube BUILD_DIR=build/lapiscube-native JOBS=$(ENGINE_JOBS) RELEASE=1 $(NATIVE_ARGS)
terminal:
	$(PYTHON) tools/prepare_engine.py --mode terminal
	$(MAKE) -C build/engine BUILD_TERMINAL=1 BUILD_DIR=build/lapiscube-terminal LIBS='-lpthread -ldl -lm' TARGET=LapisCube JOBS=$(ENGINE_JOBS) RELEASE=1
windows:
	$(PYTHON) tools/prepare_engine.py --mode windows
	$(MAKE) -C build/engine -f misc/makefiles/windows.mk TARGET=LapisCube BUILD_DIR=build/lapiscube-windows JOBS=$(ENGINE_JOBS) RELEASE=1 CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres
web:
	$(PYTHON) tools/prepare_engine.py --mode web
	$(MAKE) -C build/engine -f ../../web/build.mk RELEASE=1 -j$(ENGINE_JOBS)
	$(PYTHON) tools/package_web.py
package:
	$(PYTHON) tools/package.py
	$(PYTHON) tools/check_package.py build/LapisCube-linux.zip
clean:
	rm -rf build

web-test: probe build/survival-fixture
	$(CC) -std=c11 -Wall -Wextra -Werror -I../../include tests/web_host.c -o build/web-host-test
	./build/web-host-test
	$(PYTHON) tests/web_transport.py
