EE_BIN = ps2_stage2_loader.elf
PRINTF ?= NONE

EE_INCS = -I./include -I../../include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include
EE_LINKFILE = linkfile

EE_OBJS = main.o ps2logo.o egsm.o ee_exception_l2.o
EE_OBJS_DIR = obj/
EE_OBJS := $(EE_OBJS:%=$(EE_OBJS_DIR)%)

EE_CFLAGS += -Os -G0 -Wall -fdata-sections -ffunction-sections
EE_LDFLAGS += -Wl,-zmax-page-size=128 -Wl,--gc-sections -s

EE_NEWLIB_NANO = 1
NEWLIB_NANO = 1

EE_LIBS += -lfileXio -liopreboot -lpatches -lcdvd -lcglue -lpthread -lpthreadglue

ifeq ($(PRINTF), NONE)
else ifeq ($(PRINTF), SCR)
  EE_CFLAGS += -DSCR_PRINT
  EE_LIBS += -ldebug
else ifeq ($(PRINTF), EE_SIO)
  EE_CFLAGS += -DEE_SIO_DEBUG
  EE_LIBS += -lsiocookie
  EE_OBJS += sioprintf.o
else ifeq ($(PRINTF), PRINTF)
  EE_CFLAGS += -DCOMMON_PRINTF
else ifneq ($(PRINTF),)
  $(warning UNKNOWN PRINTF REQUESTED FOR STAGE2: '$(PRINTF)')
endif

vpath %.c src src/egsm ../../src
vpath %.S src/egsm

.PHONY: all clean

all: $(EE_BIN)

clean:
	rm -rf $(EE_OBJS_DIR) $(EE_BIN)

$(EE_OBJS_DIR):
	@mkdir -p $@

$(EE_OBJS_DIR)%.o: %.c | $(EE_OBJS_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(EE_OBJS_DIR)%.o: %.S | $(EE_OBJS_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
