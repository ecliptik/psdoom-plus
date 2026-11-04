# dos/Makefile - psdoom-plus for MS-DOS (og engine, DJGPP + SDL3-DOS + CWSDPMI)
# SPDX-License-Identifier: GPL-2.0-or-later
#
# dos/ is the sdl-dos-ports "port root" (vendor/, patches/, build/, tools/);
# the hub's shared/ layer is the subtree at ../.sdl-dos-ports/. The engine is
# not vendored here: it is psdoom-plus's og (engines/og, tools/psdev fetch og)
# with libpsd, built with the same CMakeLists as on Linux/BSD/Haiku.
#
#   make            SDL3 (patched) -> psdoom.exe, psdps.exe
#   make tests      the test TSRs (tests/*.asm -> build/*.com, needs nasm)
#   make cwsdpmi    fetch CWSDPMI r7 (sha256-pinned) into vendor/cwsdpmi/
#   make pktdrv     fetch Crynwr's NE2000 packet driver (tests only) into vendor/crynwr/
#   make check      tests/dosbox-pstest.sh: TSRs shot in DOSBox-X
#   make bundle     build/bundle/: what tools/psdev test dos ships to a guest

PORT_NAME := psdoom
REPO_ROOT := $(abspath .)
HUB_DIR   := $(abspath ../.sdl-dos-ports)
TOP       := $(abspath ..)

include $(HUB_DIR)/shared/build/sdl3-dos.mk

OG_DIR   := $(TOP)/engines/og
OG_BUILD := $(BUILD_DIR)/og
EXE      := $(BUILD_DIR)/psdoom.exe

# 486 baseline, Pentium scheduling; NOSIMD_FLAGS keeps SDL's SSE paths out.
OG_CFLAGS := $(NOSIMD_FLAGS) -march=i486 -mtune=pentium

.PHONY: all game og-src clean-game tests cwsdpmi pktdrv check bundle
all: game

game: sdl3 $(EXE) $(BUILD_DIR)/psdps.exe

og-src:
	@test -f $(OG_DIR)/src/linuxdoom-1.10/d_main.c || $(TOP)/tools/psdev fetch og

# cmake tracks the engine's own dependencies; always let it decide.
.PHONY: FORCE
$(EXE): $(SYSROOT)/lib/libSDL3.a og-src FORCE
	cmake -S $(OG_DIR) -B $(OG_BUILD) $(CMAKE_COMMON) \
	    -DCMAKE_C_FLAGS="$(OG_CFLAGS)"
	cmake --build $(OG_BUILD) -j$(NPROC)
	cp $(OG_BUILD)/psdoom-og.exe $@
	# DJGPP's default 256 KB stack is too small for the renderer's
	# recursion plus SDL; 1 MB as in the other hub ports.
	$(STUBEDIT) $@ minstack=1024k

clean-game:
	rm -rf $(OG_BUILD) $(EXE)

# psdps.exe: the MCB chain as libpsd sees it (tools/psdps.c)
$(BUILD_DIR)/psdps.exe: tools/psdps.c $(EXE)
	$(CC) -O2 -march=i486 -Wall -I$(TOP)/libpsd/include $< $(OG_BUILD)/libpsd/libpsd.a -o $@

# Test TSRs
TSRS := $(BUILD_DIR)/tsr.com $(BUILD_DIR)/hooktsr.com
tests: $(TSRS)
$(BUILD_DIR)/%.com: tests/%.asm
	@mkdir -p $(@D)
	nasm -f bin -o $@ $<

# CWSDPMI r7: the DPMI host DJGPP programs need on plain DOS. Fetched, not
# committed; csdpmi7b.zip's own cwsdpmi.doc carries its licence terms.
CWSDPMI_ZIP := https://sandmann.dotster.com/cwsdpmi/csdpmi7b.zip
CWSDPMI_SHA := 2de899fecaa90632b8b9bdfc0305cb0375e59ae252c37e32d06c1ed3f98a8f44
CWSDPMI     := $(VENDOR_DIR)/cwsdpmi/cwsdpmi.exe
cwsdpmi: $(CWSDPMI)
$(CWSDPMI):
	@mkdir -p $(@D)
	curl -fsSL $(CWSDPMI_ZIP) -o $(@D)/csdpmi7b.zip
	unzip -o -q -j $(@D)/csdpmi7b.zip bin/CWSDPMI.EXE bin/cwsdpmi.doc -d $(@D)
	mv $(@D)/CWSDPMI.EXE $@
	echo "$(CWSDPMI_SHA)  $@" | sha256sum -c --quiet

check: game tests $(CWSDPMI)
	tests/dosbox-pstest.sh

# Crynwr packet driver collection 11 (GPL): NE2000.COM for DOSBox-X's NE2000,
# used by the network tests only; never part of a release
PKTDRV_ZIP := http://crynwr.com/drivers/pktd11.zip
PKTDRV_SHA := 5dbf0d465c612c0750ad41368dc1d66876edc66f8471c9981a375779622b240c
NE2000     := $(VENDOR_DIR)/crynwr/NE2000.COM
pktdrv: $(NE2000)
$(NE2000):
	@mkdir -p $(@D)
	curl -fsSL $(PKTDRV_ZIP) -o $(@D)/pktd11.zip
	echo "$(PKTDRV_SHA)  $(@D)/pktd11.zip" | sha256sum -c --quiet
	unzip -o -q $(@D)/pktd11.zip NE2000.COM COPYING.DOC -d $(@D)

BUNDLE := $(BUILD_DIR)/bundle
bundle: game tests $(CWSDPMI) $(NE2000)
	rm -rf $(BUNDLE)
	mkdir -p $(BUNDLE)
	cp $(EXE) $(BUNDLE)/PSDOOM.EXE
	cp $(BUILD_DIR)/psdps.exe $(BUNDLE)/PSDPS.EXE
	cp $(BUILD_DIR)/tsr.com $(BUNDLE)/TSR.COM
	cp $(BUILD_DIR)/hooktsr.com $(BUNDLE)/HOOKTSR.COM
	cp $(CWSDPMI) $(BUNDLE)/CWSDPMI.EXE
	cp $(NE2000) $(BUNDLE)/NE2000.COM
	cp $(TOP)/data/psdoom1.wad $(BUNDLE)/PSDOOM1.WAD
	cp $(TOP)/data/psdoom2.wad $(BUNDLE)/PSDOOM2.WAD
