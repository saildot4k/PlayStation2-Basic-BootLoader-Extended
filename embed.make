BIN2S = @bin2c
vpath %.irx embed/iop/
vpath %.irx $(PS2SDK)/iop/irx/
IRXTAG = $(notdir $(addsuffix _irx, $(basename $<)))

# ---{ IOP BINARIES }--- #
$(EE_ASM_DIR)ioprp.c: embed/ioprp.img | $(EE_ASM_DIR)
	$(BIN2S) $< $@ psx_ioprp

ifeq ($(PSX), 1)
PSX_IOP_CACHE_DIR := $(EE_OBJS_DIR)psx_iop/
PS2SDK_LOCAL_SRC := $(abspath thirdparty/ps2sdk)

$(PSX_IOP_CACHE_DIR):
	@mkdir -p $@

$(PSX_IOP_CACHE_DIR)extflash.irx: | $(PSX_IOP_CACHE_DIR)
	@if [ -f "$(PS2SDK)/iop/irx/extflash.irx" ]; then \
		cp -f "$(PS2SDK)/iop/irx/extflash.irx" "$@"; \
	elif [ -n "$(PS2SDKSRC)" ] && [ -f "$(PS2SDKSRC)/iop/dev9/extflash/Makefile" ]; then \
		$(MAKE) -C "$(PS2SDKSRC)/iop/dev9/extflash" all; \
		cp -f "$(PS2SDKSRC)/iop/dev9/extflash/irx/extflash.irx" "$@"; \
	elif [ -f "$(PS2SDK_LOCAL_SRC)/iop/dev9/extflash/Makefile" ]; then \
		PS2SDKSRC="$(PS2SDK_LOCAL_SRC)" $(MAKE) -C "$(PS2SDK_LOCAL_SRC)/iop/dev9/extflash" all; \
		cp -f "$(PS2SDK_LOCAL_SRC)/iop/dev9/extflash/irx/extflash.irx" "$@"; \
	else \
		echo "ERROR: extflash.irx not found in \$$PS2SDK/iop/irx and no ps2sdk source tree available to build it."; \
		exit 1; \
	fi

$(PSX_IOP_CACHE_DIR)xfromman.irx: | $(PSX_IOP_CACHE_DIR)
	@if [ -f "$(PS2SDK)/iop/irx/xfromman.irx" ]; then \
		cp -f "$(PS2SDK)/iop/irx/xfromman.irx" "$@"; \
	elif [ -n "$(PS2SDKSRC)" ] && [ -f "$(PS2SDKSRC)/iop/memorycard/xfromman/Makefile" ]; then \
		$(MAKE) -C "$(PS2SDKSRC)/iop/memorycard/xfromman" all; \
		cp -f "$(PS2SDKSRC)/iop/memorycard/xfromman/irx/xfromman.irx" "$@"; \
	elif [ -f "$(PS2SDK_LOCAL_SRC)/iop/memorycard/xfromman/Makefile" ]; then \
		PS2SDKSRC="$(PS2SDK_LOCAL_SRC)" $(MAKE) -C "$(PS2SDK_LOCAL_SRC)/iop/memorycard/xfromman" all; \
		cp -f "$(PS2SDK_LOCAL_SRC)/iop/memorycard/xfromman/irx/xfromman.irx" "$@"; \
	else \
		echo "ERROR: xfromman.irx not found in \$$PS2SDK/iop/irx and no ps2sdk source tree available to build it."; \
		exit 1; \
	fi

$(EE_ASM_DIR)extflash_irx.c: $(PSX_IOP_CACHE_DIR)extflash.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ extflash_irx

$(EE_ASM_DIR)xfromman_irx.c: $(PSX_IOP_CACHE_DIR)xfromman.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ xfromman_irx
endif

$(EE_ASM_DIR)padman_irx.c: freepad.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ padman_irx

$(EE_ASM_DIR)iomanx_irx.c: iomanX.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ iomanX_irx

$(EE_ASM_DIR)filexio_irx.c: fileXio.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ fileXio_irx

$(EE_ASM_DIR)cdfs_irx.c: thirdparty/wLaunchELF_ISR/iop/__precompiled/cdfs.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ cdfs_irx

$(EE_ASM_DIR)ps2hdd_irx.c: ps2hdd-osd.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ ps2hdd_irx

$(EE_ASM_DIR)ps2ip_irx.c: ps2ip-nm.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ ps2ip_irx

$(EE_ASM_DIR)%_irx.c: %.irx
	$(DIR_GUARD)
	$(BIN2S) $< $@ $(IRXTAG)

embed/iop/mmceman.irx:
	$(info - - Downloading MMCEMAN Driver)
	wget -q https://github.com/israpps/wLaunchELF_ISR/raw/refs/heads/master/iop/__precompiled/mmceman.irx -O $@

ifeq ($(EMBED_PS1VN), 1)
PS1VN_DIR := src/ps1vn
PS1VN_ELF := $(PS1VN_DIR)/ps1vn.elf

$(PS1VN_ELF):
	$(MAKE) -C $(PS1VN_DIR)

$(EE_ASM_DIR)ps1vn_elf.c: $(PS1VN_ELF) | $(EE_ASM_DIR)
	$(BIN2S) $< $@ ps1vn_elf
endif

ifeq ($(EGSM_BUILD), 1)
PS2_STAGE2_DIR := src/ps2_stage2_loader
PS2_STAGE2_ELF := $(PS2_STAGE2_DIR)/ps2_stage2_loader.elf

$(PS2_STAGE2_ELF):
	$(MAKE) -C $(PS2_STAGE2_DIR) EGSM_BUILD=$(EGSM_BUILD)

$(EE_ASM_DIR)ps2_stage2_loader_elf.c: $(PS2_STAGE2_ELF) | $(EE_ASM_DIR)
	$(BIN2S) $< $@ ps2_stage2_loader_elf
endif

# ---{ EMBEDDED RESOURCES }--- #
$(EE_ASM_DIR)icon_sys_A.c: embed/icons/icon_A.sys | $(EE_ASM_DIR)
	$(BIN2S) $< $@ icon_sys_A

$(EE_ASM_DIR)icon_sys_J.c: embed/icons/icon_J.sys | $(EE_ASM_DIR)
	$(BIN2S) $< $@ icon_sys_J

$(EE_ASM_DIR)icon_sys_C.c: embed/icons/icon_C.sys | $(EE_ASM_DIR)
	$(BIN2S) $< $@ icon_sys_C

SPLASH_IMAGE_INPUTS = \
	assets/embedded/bg_ps2bble.png \
	assets/embedded/bg_psxbble.png \
	assets/embedded/logo_ps2bble.png \
	assets/embedded/logo_psxbble.png \
	assets/embedded/hotkeys.png

$(EE_ASM_DIR)splash_images_rbg.c: $(SPLASH_IMAGE_INPUTS) tools/png_rgba_to_rbg_c.py | $(EE_ASM_DIR)
	$(PYTHON) tools/png_rgba_to_rbg_c.py --output $@ --max-colors 255 $(SPLASH_IMAGE_INPUTS)
