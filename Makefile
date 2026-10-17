PS5_PAYLOAD_SDK ?= $(HOME)/opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
ifeq ($(shell uname -s),Darwin)
export LLVM_CONFIG ?= /opt/homebrew/opt/llvm/bin/llvm-config
endif
PS5_HOST ?= 192.168.0.190
PS5_PORT ?= 9021
CFLAGS := -Wall -Wextra -Werror -Wno-unreachable-code-generic-assoc -O2 -g
.PHONY: all probe deploy-probe clean
all: build/hltb.elf build/hltb-query.elf
probe: build/probe.elf
build/probe.elf: tools/probe.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<
deploy-probe: build/probe.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<
clean:
	rm -rf build

build/hltb-query.elf: src/main.c src/hltb.c src/http_ps5.c src/cache.c third_party/cJSON/cJSON.c src/hltb.h src/root_ca.h src/cache.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -Ithird_party/cJSON -o $@ $(filter %.c,$^) -lSceNet -lSceSsl -lSceHttp2
deploy: build/hltb.elf
	python3 tools/deploy.py $< --host $(PS5_HOST) --port $(PS5_PORT)
	python3 tools/snapshot_symbols.py

HOST_CC ?= cc
.PHONY: test deploy
test: build/test-hltb build/test-storage
	./build/test-hltb
	./build/test-storage
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
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

build/catalog-probe.elf: tools/catalog_probe.c src/catalog.c third_party/cJSON/cJSON.c src/catalog.h
	$(CC) $(CFLAGS) -Isrc -Ithird_party/cJSON -o $@ $(filter %.c,$^)

build/shell_trace.elf: src/shell_trace.c src/trace_protocol.h
	$(CC) -shared -Wl,--unresolved-symbols=ignore-all -nostartfiles -nodefaultlibs -fPIC -fno-stack-protector -O2 -Wl,-e,elf_main -Wl,-Bsymbolic -Wl,--hash-style=sysv -Wl,-z,now -o $@ $< -lkernel_sys -lSceLibcInternal
build/shell_trace_blob.h: build/shell_trace.elf
	python3 -c 'from pathlib import Path; b=Path("$<").read_bytes(); Path("$@").write_text("static const unsigned char shell_trace_blob[]={"+",".join(map(str,b))+"};\n")'
build/inject-trace.elf: tools/inject_trace.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/shell_trace_blob.h src/trace_protocol.h
	$(CXX) -std=c++20 -O2 -Isrc -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
inject-trace: build/inject-trace.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

build/remove-trace.elf: tools/inject_trace.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/shell_trace_blob.h src/trace_protocol.h
	$(CXX) -DHLTB_TRACE_REMOVE -std=c++20 -O2 -Isrc -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
remove-trace: build/remove-trace.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

# Every direct build target works after make clean.
build:
	mkdir -p build
build/shell_probe.elf build/shell_trace.elf build/loader-bridge.o build/loader-pt.o build/catalog-probe.elf: | build

NATIVE_SOURCES := src/cache.c src/playtime_native.c src/catalog.c src/catalog_snapshot.c src/hltb.c src/http_ps5.c third_party/cJSON/cJSON.c
build/shell_native.elf: $(NATIVE_SOURCES) src/native_protocol.h src/root_ca.h | build
	$(CC) -shared -Wl,--unresolved-symbols=ignore-all -nostartfiles -nodefaultlibs -fPIC -fno-stack-protector -O2 -Wno-unreachable-code-generic-assoc -Isrc -Ithird_party/cJSON -Wl,-e,elf_main -Wl,-Bsymbolic -Wl,--hash-style=sysv -Wl,-z,now -o $@ $(NATIVE_SOURCES) -lkernel_sys -lSceLibcInternal -lSceNet -lSceSsl -lSceHttp2
build/shell_native_blob.h: build/shell_native.elf
	python3 -c 'from pathlib import Path; b=Path("$<").read_bytes(); Path("$@").write_text("static const unsigned char shell_native_blob[]={"+",".join(map(str,b))+"};\n")'
build/inject-native.elf: tools/inject_native.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/catalog-export.o build/catalog-native.o build/catalog-snapshot.o build/cjson-native.o build/shell_native_blob.h src/native_protocol.h
	$(CXX) -std=c++20 -O2 -Isrc -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
build/remove-native.elf: tools/inject_native.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/catalog-export.o build/catalog-native.o build/catalog-snapshot.o build/cjson-native.o build/shell_native_blob.h src/native_protocol.h
	$(CXX) -DHLTB_NATIVE_REMOVE -std=c++20 -O2 -Isrc -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
inject-native: build/inject-native.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<
	python3 tools/snapshot_symbols.py
remove-native: build/remove-native.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

build/retry-native.elf: tools/inject_native.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/catalog-export.o build/catalog-native.o build/catalog-snapshot.o build/cjson-native.o build/shell_native_blob.h src/native_protocol.h
	$(CXX) -DHLTB_NATIVE_RETRY -std=c++20 -O2 -Isrc -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
retry-native: build/retry-native.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

build/catalog-export.o: tools/export_catalog.c src/catalog.h | build
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<
build/catalog-native.o: src/catalog.c src/catalog.h | build
	$(CC) $(CFLAGS) -Isrc -Ithird_party/cJSON -c -o $@ $<
build/catalog-snapshot.o: src/catalog_snapshot.c src/catalog.h | build
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<
build/cjson-native.o: third_party/cJSON/cJSON.c third_party/cJSON/cJSON.h | build
	$(CC) $(CFLAGS) -Ithird_party/cJSON -c -o $@ $<

build/replace-native.elf: tools/inject_native.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/catalog-export.o build/catalog-native.o build/catalog-snapshot.o build/cjson-native.o build/shell_native_blob.h src/native_protocol.h
	$(CXX) -DHLTB_NATIVE_REPLACE -std=c++20 -O2 -Isrc -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
replace-native: build/remove-native.elf build/replace-native.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) build/remove-native.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) build/replace-native.elf
	python3 tools/snapshot_symbols.py
build/enable-native.elf: tools/control_native.c | build
	$(CC) $(CFLAGS) -Isrc -o $@ $<
build/disable-native.elf: tools/control_native.c | build
	$(CC) $(CFLAGS) -Isrc -DHLTB_NATIVE_DISABLE -o $@ $<
enable-native: build/enable-native.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<
disable-native: build/disable-native.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<


# Main deliverable: one ELF injects the renderer and enables annotations.
build/hltb.elf: tools/inject_native.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/catalog-export.o build/catalog-native.o build/catalog-snapshot.o build/cjson-native.o build/shell_native_blob.h src/native_protocol.h
	$(CXX) -DHLTB_NATIVE_AUTOSTART -std=c++20 -O2 -Isrc -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)

build/test-storage: tests/test_storage.c src/cache.c src/hltb.c src/catalog_snapshot.c third_party/cJSON/cJSON.c src/cache.h src/catalog.h src/hltb.h | build
	$(HOST_CC) -Wno-deprecated-declarations -Wall -Wextra -Werror -g -fsanitize=address,undefined -DHLTB_CACHE_PATH='"cache.json"' -Isrc -Ithird_party/cJSON -o $@ $(filter %.c,$^)

build/reset-native-marker.elf: tools/control_native.c src/native_protocol.h | build
	$(CC) $(CFLAGS) -Isrc -DHLTB_NATIVE_CLEAR_MARKER -o $@ $<

# One-shot native layout metadata inspection; no widget modifications.
build/layout_probe.elf: src/layout_probe.c | build
	$(CC) -shared -Wl,--unresolved-symbols=ignore-all -nostartfiles -nodefaultlibs -fPIC -fno-stack-protector -O2 -Wl,-e,elf_main -Wl,-Bsymbolic -Wl,--hash-style=sysv -Wl,-z,now -o $@ $< -lkernel_sys -lSceLibcInternal
build/layout_probe_blob.h: build/layout_probe.elf
	python3 -c 'from pathlib import Path; b=Path("$<").read_bytes(); Path("$@").write_text("static const unsigned char layout_probe_blob[]={"+",".join(map(str,b))+"};\n")'
build/inspect-layout.elf: tools/inject_probe.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/layout_probe_blob.h
	$(CXX) -DHLTB_LAYOUT_PROBE -std=c++20 -O2 -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
inspect-layout: build/inspect-layout.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

build/render_probe.elf: src/render_probe.c src/native_protocol.h | build
	$(CC) -DHLTB_RENDER_PROBE -shared -Wl,--unresolved-symbols=ignore-all -nostartfiles -nodefaultlibs -fPIC -fno-stack-protector -O2 -Isrc -Wl,-e,elf_main -Wl,-Bsymbolic -Wl,--hash-style=sysv -Wl,-z,now -o $@ $< -lkernel_sys -lSceLibcInternal
build/render_probe_blob.h: build/render_probe.elf
	python3 -c 'from pathlib import Path; b=Path("$<").read_bytes(); Path("$@").write_text("static const unsigned char render_probe_blob[]={"+",".join(map(str,b))+"};\n")'
build/inject-render-probe.elf: tools/inject_native.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/render_probe_blob.h src/native_protocol.h
	$(CXX) -DHLTB_RENDER_PROBE -std=c++20 -O2 -Isrc -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
build/remove-render-probe.elf: tools/inject_native.cpp $(LOADER)/stable_shellui_injector.cpp $(LOADER)/remote_call.cpp build/loader-bridge.o build/loader-pt.o build/render_probe_blob.h src/native_protocol.h
	$(CXX) -DHLTB_RENDER_PROBE -DHLTB_NATIVE_REMOVE -std=c++20 -O2 -Isrc -Ibuild -I$(LOADER) -I$(SHSRV) -o $@ $(filter %.cpp %.o,$^)
inject-render-probe: build/inject-render-probe.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<
remove-render-probe: build/remove-render-probe.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<
