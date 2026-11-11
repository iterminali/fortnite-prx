# ============================================================
# fortnite.prx — GoldHEN plugin (PS4 / Orbis)
# Toolchain : OpenOrbis-PS4-Toolchain  (OO_PS4_TOOLCHAIN)
# SDK       : GoldHEN_Plugins_SDK      (GOLDHEN_SDK)
# ============================================================

PROJECT      := fortnite
BUILD_FOLDER := bin
PROJDIR      := source
INCLUDEDIR   := include
INTDIR       := build

TARGET       := $(BUILD_FOLDER)/$(PROJECT)
TARGET_ELF   := $(TARGET).elf
TARGET_PRX   := $(TARGET).prx
TARGETSTUB   := $(TARGET)_stub.so

TOOLCHAIN := $(OO_PS4_TOOLCHAIN)
GH_SDK    := $(GOLDHEN_SDK)

# --- sanity -------------------------------------------------
ifeq ($(strip $(TOOLCHAIN)),)
$(error OO_PS4_TOOLCHAIN is not set)
endif
ifeq ($(strip $(GH_SDK)),)
$(error GOLDHEN_SDK is not set)
endif

# --- libs ---------------------------------------------------
# GoldHEN_Hook must come before kernel; it pulls khook + the
# patch engine that wraps the Orbis imports.
LIBS := -lGoldHEN_Hook -lkernel -lSceLibcInternal -lSceSysmodule -lScePad

O_FLAG     := -O3
EXTRAFLAGS := -D__USE_KLOG__ -D__FINAL__=1

# --- sources ------------------------------------------------
CFILES   := $(wildcard $(PROJDIR)/*.c)
OBJS     := $(patsubst $(PROJDIR)/%.c,$(INTDIR)/%.o,$(CFILES))
STUBOBJS := $(patsubst $(PROJDIR)/%.c,$(INTDIR)/%.o.stub,$(CFILES))

# --- flags --------------------------------------------------
CFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
          $(EXTRAFLAGS) \
          -isysroot $(TOOLCHAIN) \
          -isystem $(TOOLCHAIN)/include \
          -I$(GH_SDK)/include \
          -I$(INCLUDEDIR) \
          $(O_FLAG)

LDFLAGS := -m elf_x86_64 -pie \
           --script $(TOOLCHAIN)/link.x \
           -e _init --eh-frame-hdr \
           -L$(TOOLCHAIN)/lib -L$(GH_SDK) \
           $(LIBS)

# --- toolchain selection ------------------------------------
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CC := clang
  LD := ld.lld
  CDIR := linux
endif
ifeq ($(UNAME_S),Darwin)
  CC := /usr/local/opt/llvm/bin/clang
  LD := /usr/local/opt/llvm/bin/ld.lld
  CDIR := macos
endif

# --- rules --------------------------------------------------
.PHONY: all clean dirs

all: dirs $(TARGET_PRX)

dirs:
	@mkdir -p $(INTDIR) $(BUILD_FOLDER)

$(INTDIR)/%.o: $(PROJDIR)/%.c | dirs
	$(CC) $(CFLAGS) -o $@ $<

# stub objects: compiled without the Orbis sysroot so the
# resulting .so exports g_pluginName / g_pluginDesc / etc.
$(INTDIR)/%.o.stub: $(PROJDIR)/%.c | dirs
	$(CC) -target x86_64-pc-linux-gnu -fPIC -shared -O2 \
	      -I$(INCLUDEDIR) -I$(GH_SDK)/include \
	      -D__STUB__ -o $@ $<

$(TARGET_ELF): $(OBJS) | dirs
	$(LD) $(GH_SDK)/build/crtprx.o $(OBJS) -o $@ $(LDFLAGS)

$(TARGET_PRX): $(TARGET_ELF)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself \
	    -in=$(TARGET_ELF) \
	    -out=$(TARGET).oelf \
	    --lib=$(TARGET_PRX) \
	    --paid 0x3800000000000011

$(TARGETSTUB): $(STUBOBJS) | dirs
	$(CC) $(STUBOBJS) -o $(TARGETSTUB) \
	      -target x86_64-pc-linux-gnu -shared

clean:
	rm -rf $(INTDIR) $(BUILD_FOLDER) $(TARGET).oelf
