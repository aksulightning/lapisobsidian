CC ?= cc
PYTHON ?= python3
ENGINE := engine
CPPFLAGS += -Isrc -I$(ENGINE)/src
CFLAGS ?= -O2 -g
WARN := -std=c89 -Wall -Wextra -Werror -Wshadow -Wconversion
CORE := src/LapisProtocol.c src/LapisSession.c
IDENTITY := src/LapisIdentity.c build/md5.o build/dec32le.o build/enc32le.o

.PHONY: all test sanitize probe integration native-smoke engine-prepare native terminal clean
all: test probe
build:
	mkdir -p build
build/%.o: $(ENGINE)/third_party/bearssl/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/protocol-test: tests/protocol.c $(CORE) $(IDENTITY) src/*.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/protocol.c $(CORE) $(IDENTITY) -o $@
test: build/protocol-test
	./build/protocol-test
sanitize: build/md5.o build/dec32le.o build/enc32le.o | build
	$(CC) $(CPPFLAGS) $(SANITIZER_FLAGS) -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/protocol.c $(CORE) $(IDENTITY) -o build/protocol-sanitize
	./build/protocol-sanitize
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
	$(MAKE) -C build/engine TARGET=LapisCube
terminal:
	$(PYTHON) tools/prepare_engine.py --mode terminal
	$(MAKE) -C build/engine BUILD_TERMINAL=1 LIBS='-lpthread -ldl -lm' TARGET=LapisCube
clean:
	rm -rf build
