BIN2S = @bin2c
vpath %.irx embed/iop/
vpath %.irx $(PS2SDK)/iop/irx/
IRXTAG = $(notdir $(addsuffix _irx, $(basename $<)))

# ---{ IOP BINARIES }--- #
$(EE_ASM_DIR)ioprp.c: embed/ioprp.img | $(EE_ASM_DIR)
	$(BIN2S) $< $@ psx_ioprp

$(EE_ASM_DIR)padman_irx.c: freepad.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ padman_irx

$(EE_ASM_DIR)iomanx_irx.c: iomanX.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ iomanX_irx

$(EE_ASM_DIR)filexio_irx.c: fileXio.irx | $(EE_ASM_DIR)
	$(BIN2S) $< $@ fileXio_irx

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
PS1VN_DIR := thirdparty/ps1vn
PS1VN_ELF := $(PS1VN_DIR)/ps1vn.elf

$(PS1VN_ELF):
	$(MAKE) -C $(PS1VN_DIR)

$(EE_ASM_DIR)ps1vn_elf.c: $(PS1VN_ELF) | $(EE_ASM_DIR)
	$(BIN2S) $< $@ ps1vn_elf
endif

ifeq ($(EMBED_PS2_STAGE2), 1)
PS2_STAGE2_DIR := thirdparty/ps2_stage2_loader
PS2_STAGE2_ELF := $(PS2_STAGE2_DIR)/ps2_stage2_loader.elf

$(PS2_STAGE2_ELF):
	$(MAKE) -C $(PS2_STAGE2_DIR) PRINTF=$(PRINTF)

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
	$(PYTHON) tools/png_rgba_to_rbg_c.py --output $@ $(SPLASH_IMAGE_INPUTS)
