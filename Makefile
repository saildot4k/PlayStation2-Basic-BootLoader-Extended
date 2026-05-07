define HEADER
__________  _________________   ____________________.____
\______   \/   _____/\_____  \  \______   \______   \    |
 |     ___/\_____  \  /  ____/   |    |  _/|    |  _/    |
 |    |    /        \/       \   |    |   \|    |   \    |___
 |____|   /_______  /\_______ \  |______  /|______  /_______ \\
                  \/         \/         \/        \/        \/
		PlayStation2 Basic BootLoader - By El_isra
endef
export HEADER


# ---{BUILD CFG}--- #
HAS_EMBED_IRX ?= 1                  # whether to embed or not non vital IRX (wich will be loaded from memcard files)
DEBUG ?= 0
PSX ?= 0                            # PSX DESR support
HDD ?= 0                            # Internal HDD support
MMCE ?= 0
MX4SIO ?= 0
PROHBIT_DVD_0100 ?= 0               # prohibit the DVD Players v1.00 and v1.01 from being booted.
XCDVD_READKEY ?= 0                  # Enable the newer sceCdReadKey checks, which are only supported by a newer CDVDMAN module.
UDPTTY ?= 0                         # printf over UDP
PPCTTY ?= 0                         # printf over PowerPC UART
PRINTF ?= NONE
EMBED_PS1VN ?= 1                    # embed PS1VModeNegator (PS1VN) for PS1 discs; set 0 to load external PS1VN.ELF
EGSM_BUILD ?= 1                     # build the embedded stage2 eGSM runtime (0=disabled, 1=enabled)
PSX_ALL_DRIVERS_LAZY_LOADING ?= 1
BDM_ATA ?= 0
DISC_STOP_AT_BOOT ?= 0              # stop optical disc after config bootstrap when booted from disc (disc-boot profile)

HOMEBREW_IRX ?= 0                   # if we need homebrew SIO2MAN, MCMAN, MCSERV & PADMAN embedded, else, builtin console drivers are used
FILEXIO_NEED ?= 0                   # if we need filexio and imanx loaded for other features (HDD, mx4sio, etc)
DEV9_NEED ?= 0                      # if we need DEV9 loaded for other features (HDD, UDPTTY, etc)

# Related to binary size reduction (it disables some features, please be sure you won't disable something you need)
KERNEL_NOPATCH = 0
NEWLIB_NANO = 1
DUMMY_TIMEZONE = 1

# ---{ VERSIONING }--- #

VERSION = 2
SUBVERSION = 0
PATCHLEVEL = 0
STATUS = Beta

# Prefer python3, fall back to python for CI images that don't ship python3 binary name.
PYTHON ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null)

# ---{ EXECUTABLES }--- #

BINDIR ?= bin/
BASENAME ?= PS2BBL
EE_BIN = $(BINDIR)$(BASENAME).ELF
EE_BIN_STRIPPED = $(BINDIR)stripped_$(BASENAME).ELF
EE_BIN_PACKED = $(BINDIR)COMPRESSED_$(BASENAME).ELF
KELFTYPE ?= MC
EE_BIN_ENCRYPTED = $(BINDIR)$(BASENAME)_$(KELFTYPE).KELF

# ---{ OBJECTS & STUFF }--- #

EE_OBJS_DIR = obj/
EE_SRC_DIR = src/
EE_ASM_DIR = asm/

EE_OBJS = main.o common_data.o console_info.o loader_config.o loader_launch.o loader_path.o loader_video.o loader_video_selector.o loader_disc.o loader_modules.o loader_hdd.o loader_platform.o loader_runtime.o loader_utils.o loader_defaults.o loader_ui.o \
          util.o elf.o timer.o ps2.o ps1.o dvdplayer.o egsm_parse.o \
          libcdvd_add.o OSDHistory.o OSDInit.o OSDConfig.o game_id.o game_id_table.o splash_screen.o splash_assets.o splash_render.o \
          xparam_irx.o \
          $(EMBEDDED_STUFF) \
			      $(IOP_OBJS)

ifeq ($(EGSM_BUILD), 1)
  $(info --- building with eGSM via embedded PS2 stage2)
  EE_CFLAGS += -DEGSM_BUILD=1
  EE_OBJS += ps2_stage2_loader_elf.o
else
  $(info --- building with eGSM runtime disabled)
  EE_CFLAGS += -DEGSM_BUILD=0
endif

EMBEDDED_STUFF = icon_sys_A.o icon_sys_J.o icon_sys_C.o splash_images_rbg.o

EE_CFLAGS += -Wall
EE_CFLAGS += -fdata-sections -ffunction-sections -DREPORT_FATAL_ERRORS
EE_LDFLAGS += -L$(PS2SDK)/ports/lib -L$(PS2DEV)/gsKit/lib
EE_LDFLAGS += -Wl,--gc-sections -Wno-sign-compare
# Keep default PS2SDK linkfile for runtime stability.
EE_LIBS += -ldebug -lmc -lpatches -lgskit -ldmakit
EE_INCS += -Iinclude -I$(PS2SDK)/ports/include -I$(PS2SDK)/common/include -I$(PS2DEV)/gsKit/include
EE_CFLAGS += -DVERSION=\"$(VERSION)\" -DSUBVERSION=\"$(SUBVERSION)\" -DPATCHLEVEL=\"$(PATCHLEVEL)\" -DSTATUS=\"$(STATUS)\"

$(EE_OBJS_DIR)ps2.o: EE_CFLAGS += -G0 -Os

# ---{ CONDITIONS }--- #

ifneq ($(VERBOSE), 1)
   .SILENT:
endif

ifeq ($(DISC_STOP_AT_BOOT), 1)
  $(info --- disc stop at startup enabled)
  EE_CFLAGS += -DDISC_STOP_AT_BOOT
endif

ifeq ($(PSX_ALL_DRIVERS_LAZY_LOADING), 1)
  $(info --- profile: PSX-ALL-DRIVERS-LAZY-LOADING)
  PSX = 1
  HDD = 1
  MMCE = 1
  MX4SIO = 1
  HAS_EMBED_IRX = 1
  BDM_ATA = 1
  HOMEBREW_IRX = 1
  FILEXIO_NEED = 1
  DEV9_NEED = 1
endif

ifeq ($(MX4SIO), 1)
  HOMEBREW_IRX = 1
  FILEXIO_NEED = 1
  EE_OBJS += mx4sio_bd_mini_irx.o
  EE_CFLAGS += -DMX4SIO
  ifeq ($(USE_ROM_SIO2MAN), 1)
    $(error MX4SIO needs Homebrew SIO2MAN to work)
  endif
endif

ifeq ($(MMCE), 1)
  HOMEBREW_IRX = 1
  FILEXIO_NEED = 1
  EE_OBJS += mmceman_irx.o
  EE_CFLAGS += -DMMCE
  ifeq ($(USE_ROM_SIO2MAN), 1)
    $(error MMCE needs Homebrew SIO2MAN to work)
  endif
  ifeq ($(MX4SIO), 1)
    ifneq ($(PSX_ALL_DRIVERS_LAZY_LOADING), 1)
      $(error MX4SIO cant coexist with MMCE)
    endif
  endif
endif

ifeq ($(HOMEBREW_IRX), 1)
   $(info --- enforcing usage of homebrew IRX modules)
   USE_ROM_PADMAN = 0
   USE_ROM_MCMAN = 0
   USE_ROM_SIO2MAN = 0
else
   $(info --- using BOOT-ROM drivers)
   USE_ROM_PADMAN = 1
   USE_ROM_MCMAN = 1
   USE_ROM_SIO2MAN = 1
endif

ifeq ($(PSX), 1)
   $(info --- building with PSX-DESR support)
  ifeq ($(PSX_ALL_DRIVERS_LAZY_LOADING), 1)
    BASENAME = PSX-ALL-DRIVERS-LAZY-LOADING
  else
    BASENAME = PSXBBL
  endif
  EE_CFLAGS += -DPSX=1
  EE_OBJS += scmd_add.o ioprp.o extflash_irx.o xfromman_irx.o
  EE_LIBS += -lxcdvd -liopreboot
else
  EE_LIBS += -lcdvd
endif

ifeq ($(DEBUG), 1)
   $(info --- debugging enabled)
  EE_CFLAGS += -DDEBUG -O0 -g
  EE_LIBS += -lelf-loader
else
  EE_CFLAGS += -Os
  EE_LDFLAGS += -s
  EE_LIBS += -lelf-loader-nocolour
endif

ifeq ($(EMBED_PS1VN), 1)
  $(info --- embedding PS1VN)
  EE_CFLAGS += -DEMBED_PS1VN
  EE_OBJS += ps1vn_elf.o
endif


ifeq ($(USE_ROM_PADMAN), 1)
  EE_CFLAGS += -DUSE_ROM_PADMAN
  EE_LIBS += -lpad
  EE_OBJS += pad.o
else
  EE_OBJS += pad.o padman_irx.o
  EE_LIBS += -lpadx
endif

ifeq ($(USE_ROM_MCMAN), 1)
  EE_CFLAGS += -DUSE_ROM_MCMAN
else
  EE_OBJS += mcman_irx.o mcserv_irx.o
endif

ifeq ($(USE_ROM_SIO2MAN), 1)
  EE_CFLAGS += -DUSE_ROM_SIO2MAN
else
  EE_OBJS += sio2man_irx.o
endif

ifeq ($(HAS_EMBED_IRX), 1)
  $(info --- USB drivers will be embedded)
  EE_OBJS += usbd_mini_irx.o bdm_irx.o bdmfs_fatfs_irx.o usbmass_bd_mini_irx.o
  EE_CFLAGS += -DHAS_EMBEDDED_IRX
else
  $(info --- USB drivers will be external)
endif

ifeq ($(BDM_ATA), 1)
  $(info --- ATA BDM driver will be embedded)
  EE_CFLAGS += -DBDM_ATA
  EE_OBJS += ata_bd_irx.o
  DEV9_NEED = 1
endif

ifeq ($(HDD), 1)
  $(info --- compiling with HDD support)
  EE_LIBS += -lpoweroff
  EE_OBJS += ps2fs_irx.o ps2hdd_irx.o ps2atad_irx.o poweroff_irx.o
  EE_CFLAGS += -DHDD
  FILEXIO_NEED = 1
  DEV9_NEED = 1
  KELFTYPE = HDD
endif

ifeq ($(UDPTTY), 1)
  $(info --- UDPTTY enabled)
  EE_CFLAGS += -DUDPTTY
  EE_OBJS += udptty_irx.o ps2ip_irx.o netman_irx.o smap_irx.o
  DEV9_NEED = 1
  ifneq ($(PRINTF), EE_SIO) # only enable common printf if EE_SIO is disabled. this allows separating EE and IOP printf
    PRINTF = PRINTF
  endif
else ifeq ($(PPCTTY), 1)
  $(info --- PPCTTY enabled)
  EE_CFLAGS += -DPPCTTY
  EE_OBJS += ppctty_irx.o
  ifneq ($(PRINTF), EE_SIO) # only enable common printf if EE_SIO is disabled. this allows separating EE and IOP printf
    PRINTF = PRINTF
  endif
endif

ifeq ($(FILEXIO_NEED), 1)
  $(info --- FILEXIO will be included)
  EE_CFLAGS += -DFILEXIO
  EE_LIBS += -lfileXio
  EE_OBJS += filexio_irx.o iomanx_irx.o
endif

ifeq ($(DUMMY_TIMEZONE), 1)
  EE_CFLAGS += -DDUMMY_TIMEZONE
endif

ifeq ($(DEV9_NEED), 1)
  EE_CFLAGS += -DDEV9
  EE_OBJS += ps2dev9_irx.o
endif

ifdef COMMIT_HASH
  EE_CFLAGS += -DCOMMIT_HASH=\"$(COMMIT_HASH)\"
else
  EE_CFLAGS += -DCOMMIT_HASH=\"$(shell git rev-parse --short HEAD)\"
endif

ifeq ($(DUMMY_LIBC_INIT), 1)
  EE_CFLAGS += -DDUMMY_LIBC_INIT
endif

ifeq ($(KERNEL_NOPATCH), 1)
  EE_CFLAGS += -DKERNEL_NOPATCH
endif

ifeq ($(PRINTF), NONE)
else ifeq ($(PRINTF), SCR)
  $(info --- SCR Printf enabled)
  EE_CFLAGS += -DSCR_PRINT
else ifeq ($(PRINTF), EE_SIO)
  $(info --- EESIO Printf enabled)
  EE_CFLAGS += -DEE_SIO_DEBUG
  EE_LIBS += -lsiocookie
  EE_OBJS += sioprintf.o
else ifeq ($(PRINTF), PRINTF)
  $(info --- Common Printf enabled)
  EE_CFLAGS += -DCOMMON_PRINTF
else ifneq ($(PRINTF),)
  $(warning UNKNOWN PRINTF REQUESTED: '$(PRINTF)')
endif

ifeq ($(XCDVD_READKEY),1)
  EE_CFLAGS += -DXCDVD_READKEY=1
endif

ifeq ($(PROHBIT_DVD_0100),1)
  EE_CFLAGS += -DPROHBIT_DVD_0100=1
endif

# ---{ RECIPES }--- #
.PHONY: greeting debug all clean clean-subprojects kelf packed release rebuild banner analyze clean

all: $(EE_BIN)
ifeq ($(DEBUG), 1)
	$(MAKE) greeting
endif

debug:
	$(MAKE) all DEBUG=1

rebuild:
	$(MAKE) clean
	$(MAKE) packed

packed: $(EE_BIN_PACKED)

RELEASE_TARGET = $(EE_BIN_PACKED)

greeting:
	@echo built PS2BBL PSX=$(PSX), LOCAL_IRX=$(HAS_EMBED_IRX), DEBUG=$(DEBUG)
	@echo PROHBIT_DVD_0100=$(PROHBIT_DVD_0100), XCDVD_READKEY=$(XCDVD_READKEY)
	@echo KERNEL_NOPATCH=$(KERNEL_NOPATCH), NEWLIB_NANO=$(NEWLIB_NANO)
	@echo binaries dispatched to $(BINDIR)
	@echo printf=$(PRINTF)
	@echo $(EE_OBJS)

release:
	$(MAKE) clean
	$(MAKE) $(RELEASE_TARGET)
	@rm -f $(EE_BIN_STRIPPED)
	@echo "$$HEADER"

clean-subprojects:
	@if [ -f src/ps1vn/Makefile ]; then $(MAKE) -C src/ps1vn clean; fi
	@if [ -f src/ps2_stage2_loader/Makefile ]; then $(MAKE) -C src/ps2_stage2_loader clean; fi

clean:
	@rm -rf $(EE_BIN) $(EE_BIN_STRIPPED) $(EE_BIN_ENCRYPTED) $(EE_BIN_PACKED)
	@rm -rf $(EE_OBJS_DIR) $(EE_ASM_DIR)
	@$(MAKE) clean-subprojects

$(EE_BIN_STRIPPED): $(EE_BIN)
	@echo " -- Stripping"
	$(EE_STRIP) -o $@ $<

$(EE_BIN_PACKED): $(EE_BIN_STRIPPED)
	@echo " -- Compressing"
ifneq ($(DEBUG),1)
	ps2-packer $< $@
else
	ps2-packer -v $< $@
endif

$(EE_BIN_ENCRYPTED): $(EE_BIN_PACKED)
	@echo " -- Encrypting ($(KELFTYPE))"
ifeq ($(KELFTYPE), MC)
	tools/kelftool encrypt dnasload $< $@
else ifeq ($(KELFTYPE), HDD)
	tools/kelftool encrypt fhdb $< $@
else
	$(error UNKNOWN KELF TYPE: '$(KELFTYPE)')
endif
# move OBJ to folder and search source on src/, borrowed from OPL makefile

EE_OBJS := $(EE_OBJS:%=$(EE_OBJS_DIR)%) # remap all EE_OBJ to obj subdir

$(EE_OBJS_DIR):
	@mkdir -p $@

$(EE_ASM_DIR):
	@mkdir -p $@

$(BINDIR):
	@mkdir -p $@

vpath %.c $(EE_ASM_DIR)
$(EE_OBJS_DIR)%.o: $(EE_SRC_DIR)%.c | $(EE_OBJS_DIR)
ifneq ($(VERBOSE),1)
	@echo "  - $@"
endif
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(EE_OBJS_DIR)%.o: $(EE_ASM_DIR)%.c | $(EE_OBJS_DIR)
ifneq ($(VERBOSE),1)
	@echo "  - $@"
endif
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(EE_OBJS_DIR)%.o: $(EE_ASM_DIR)%.s | $(EE_OBJS_DIR)
ifneq ($(VERBOSE),1)
	@echo "  - $@"
endif
	$(EE_AS) $(EE_ASFLAGS) $< -o $@

$(EE_OBJS_DIR)%.o: $(EE_SRC_DIR)%.S | $(EE_OBJS_DIR)
ifneq ($(VERBOSE),1)
	@echo "  - $@"
endif
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
#
analize:
	$(MAKE) rebuild DEBUG=1
	python3 tools/elf-size-analize.py $(EE_BIN) -R -t mips64r5900el-ps2-elf-

celan: clean # a repetitive typo when quicktyping
kelf: $(EE_BIN_ENCRYPTED) # alias of KELF creation


banner:
	@echo "$$HEADER"

# Include makefiles
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
include embed.make
