PS5_PAYLOAD_SDK ?= $(HOME)/opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
export LLVM_CONFIG ?= /opt/homebrew/opt/llvm/bin/llvm-config
CFLAGS := -Wall -Wextra -Werror -Wno-unreachable-code-generic-assoc -O2 -g
.PHONY: all probe deploy-probe clean
all: build/hltb.elf
probe: build/probe.elf
build/probe.elf: tools/probe.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<
deploy-probe: build/probe.elf
	$(PS5_DEPLOY) -h 192.168.0.190 $<
clean:
	rm -rf build

build/hltb.elf: src/main.c src/hltb.c src/http_ps5.c third_party/cJSON/cJSON.c src/hltb.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -Ithird_party/cJSON -o $@ $(filter %.c,$^) -lSceNet -lSceSsl -lSceHttp2
deploy: build/hltb.elf
	$(PS5_DEPLOY) -h 192.168.0.190 $<

HOST_CC ?= cc
.PHONY: test deploy
test: build/test-hltb
	./build/test-hltb
build/test-hltb: tests/test_hltb.c src/hltb.c third_party/cJSON/cJSON.c src/hltb.h
	mkdir -p build
	$(HOST_CC) -Wno-deprecated-declarations -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc -Ithird_party/cJSON -o $@ $(filter %.c,$^)

LOADER := third_party/overlay-loader
SHSRV := third_party/shsrv
build/shell_probe.elf: src/shell_probe.c
	$(CC) -shared -Wl,--unresolved-symbols=ignore-all -nostartfiles -nodefaultlibs -fPIC -fno-stack-protector -O2 -Wl,-e,elf_main -Wl,-Bsymbolic -Wl,--hash-style=sysv -Wl,-z,now -o $@ $< -lkernel_sys -lSceLibcInternal
build/shell_probe_blob.h: build/shell_probe.elf
	python3 -c 'from pathlib import Path; b=Path("$<").read_bytes(); Path("$@").write_text("static const unsigned char shell_probe_blob[]={"+",".join(map(str,b))+"};\n")'
build/loader-bridge.o: $(LOADER)/stable_elfldr_bridge.c
	$(CC) -O2 -I$(SHSRV) -c -o $@ $<
build/loader-pt.o: $(SHSRV)/pt.c
	$(CC) -O2 -I$(SHSRV) -c -o $@ $<
build/inject-probe.elf: tools/inject_probe.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/shell_probe_blob.h
	$(CXX) -std=c++20 -O2 -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
inject-probe: build/inject-probe.elf
	$(PS5_DEPLOY) -h 192.168.0.190 $<

build/catalog-probe.elf: tools/catalog_probe.c src/catalog.c third_party/cJSON/cJSON.c src/catalog.h
	$(CC) $(CFLAGS) -Isrc -Ithird_party/cJSON -o $@ $(filter %.c,$^)
