CC ?= cc
PYTHON ?= python3
ENGINE_JOBS ?= 2
ENGINE := engine
CPPFLAGS += -Isrc -I$(ENGINE)/src
CFLAGS ?= -O2 -g
WARN := -std=c89 -Wall -Wextra -Werror -Wshadow -Wconversion
CORE := src/LapisProtocol.c src/LapisSession.c src/LapisWorld.c src/LapisGameplay.c src/LapisEntities.c src/LapisEffects.c
IDENTITY := src/LapisIdentity.c build/md5.o build/dec32le.o build/enc32le.o

.PHONY: all test sanitize probe integration native-smoke engine-prepare native terminal windows package clean
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
sanitize: build/md5.o build/dec32le.o build/enc32le.o | build
	$(CC) $(CPPFLAGS) $(SANITIZER_FLAGS) -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/protocol.c $(CORE) $(IDENTITY) -o build/protocol-sanitize
	./build/protocol-sanitize
	$(CC) $(CPPFLAGS) $(SANITIZER_FLAGS) -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/gameplay.c $(CORE) -o build/gameplay-sanitize
	./build/gameplay-sanitize
build/lapiscube-probe: tools/probe.c $(CORE) $(IDENTITY) src/*.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -Wall -Wextra -Werror -Wshadow -Wconversion tools/probe.c $(CORE) $(IDENTITY) -o $@
probe: build/lapiscube-probe
integration: probe
	$(PYTHON) tests/integration.py
native-smoke: terminal
	$(PYTHON) tests/native_smoke.py
engine-prepare:
	$(PYTHON) tools/prepare_engine.py
native:
	$(PYTHON) tools/prepare_engine.py --mode native
	$(MAKE) -C build/engine TARGET=LapisCube JOBS=$(ENGINE_JOBS) RELEASE=1 $(NATIVE_ARGS)
terminal:
	$(PYTHON) tools/prepare_engine.py --mode terminal
	$(MAKE) -C build/engine BUILD_TERMINAL=1 LIBS='-lpthread -ldl -lm' TARGET=LapisCube JOBS=$(ENGINE_JOBS) RELEASE=1
windows:
	$(PYTHON) tools/prepare_engine.py --mode windows
	$(MAKE) -C build/engine -f misc/makefiles/windows.mk TARGET=LapisCube JOBS=$(ENGINE_JOBS) RELEASE=1 CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres
package:
	$(PYTHON) tools/package.py
clean:
	rm -rf build
