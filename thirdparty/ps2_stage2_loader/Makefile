EE_BIN = ps2_stage2_loader.elf

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

vpath %.c src src/egsm
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
