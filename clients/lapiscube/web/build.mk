# Invoked in the staged engine. The upstream checkout remains pristine.
SOURCE_DIRS := src src/webclient
BUILD_DIR := build/lapiscube-web
TARGET := LapisCube
CC := emcc
CFLAGS := -O2
LDFLAGS := -O2 -sWASM=1 -sNO_EXIT_RUNTIME=1 -sABORTING_MALLOC=0 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sMAXIMUM_MEMORY=268435456 -sSTACK_SIZE=524288 -sENVIRONMENT=web -sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=1
LIBS := --js-library src/webclient/interop_web.js
include misc/makefiles/common_config.mk
OEXT := .js
OBJECTS := build/lapiscube-web/md5.o build/lapiscube-web/dec32le.o build/lapiscube-web/enc32le.o
include misc/makefiles/common_build.mk
build/lapiscube-web/%.o: third_party/bearssl/%.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) -c $< -o $@
include misc/makefiles/common_targets.mk
