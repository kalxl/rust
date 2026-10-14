CXX      = x86_64-w64-mingw32-g++-posix
CC       = x86_64-w64-mingw32-gcc-posix
WINDRES  = x86_64-w64-mingw32-windres

OUTDIR   = bin
CXXFLAGS = -std=c++20 -O2 -Wall \
           -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
           -I/usr/x86_64-w64-mingw32/include

# ── Injector EXE ─────────────────────────────────────────
INJ_SRC  = src/injector.cpp
INJ_OUT  = $(OUTDIR)/RustInjector.exe
INJ_LIBS = -ladvapi32 -lshlwapi -mconsole

# ── Payload DLL ───────────────────────────────────────────
DLL_SRC  = dll/src/dllmain.cpp \
           dll/src/hook.cpp \
           dll/src/overlay.cpp \
           dll/src/fps.cpp \
           dll/src/keystrokes.cpp \
           dll/src/menu.cpp
DLL_OUT  = $(OUTDIR)/RustDLL.dll
DLL_LIBS = -ld3d11 -ld3d12 -ldxgi -ld2d1 -ldwrite \
           -ldwmapi -ladvapi32 \
           -static-libgcc -static-libstdc++

.PHONY: all clean

all: $(OUTDIR) $(INJ_OUT) $(DLL_OUT)
	@echo ""
	@echo "Build complete:"
	@echo "  $(INJ_OUT)"
	@echo "  $(DLL_OUT)"

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(INJ_OUT): $(INJ_SRC)
	$(CXX) $(CXXFLAGS) -mconsole -o $@ $^ $(INJ_LIBS)

$(DLL_OUT): $(DLL_SRC)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^ $(DLL_LIBS)

clean:
	rm -rf $(OUTDIR)
