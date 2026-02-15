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

# ---{ EMBEDDED RESOURCES }--- #
$(EE_ASM_DIR)icon_sys_A.c: embed/icons/icon_A.sys | $(EE_ASM_DIR)
	$(BIN2S) $< $@ icon_sys_A

$(EE_ASM_DIR)icon_sys_J.c: embed/icons/icon_J.sys | $(EE_ASM_DIR)
	$(BIN2S) $< $@ icon_sys_J

$(EE_ASM_DIR)icon_sys_C.c: embed/icons/icon_C.sys | $(EE_ASM_DIR)
	$(BIN2S) $< $@ icon_sys_C

$(EE_ASM_DIR)ps2bble_splash_template_png.c: assets/PS2BBLE_Splash_Template.png | $(EE_ASM_DIR)
	$(BIN2S) $< $@ ps2bble_splash_template_png

$(EE_ASM_DIR)psxbble_splash_template_png.c: assets/PSXBBLE_Splash_Template.png | $(EE_ASM_DIR)
	$(BIN2S) $< $@ psxbble_splash_template_png

TRANSPARENT_PS2_SRC = assets/transparent_ps2bble\ 300x62.png
TRANSPARENT_PSX_SRC = assets/transparent_psxbble\ 300x62.png

$(EE_ASM_DIR)transparent_ps2bble_png.c: $(TRANSPARENT_PS2_SRC) | $(EE_ASM_DIR)
	$(BIN2S) "$<" $@ transparent_ps2bble_png

$(EE_ASM_DIR)transparent_psxbble_png.c: $(TRANSPARENT_PSX_SRC) | $(EE_ASM_DIR)
	$(BIN2S) "$<" $@ transparent_psxbble_png
