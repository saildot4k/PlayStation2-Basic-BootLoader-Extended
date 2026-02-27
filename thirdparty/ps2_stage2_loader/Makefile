EE_BIN = ps2_stage2_loader.elf

EE_INCS = -I./include -I../../include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include

EE_LINKFILE = linkfile
EE_OBJS := main.o egsm.o ee_exception_l2.o

EE_CFLAGS := -D_EE -Os -G0 -Wall $(EE_CFLAGS)
EE_CFLAGS += -fdata-sections -ffunction-sections
EE_LDFLAGS += -Wl,-zmax-page-size=128 -Wl,--gc-sections -s

EE_NEWLIB_NANO = 1
NEWLIB_NANO = 1

EE_OBJS_DIR = obj/
EE_SRC_DIR = src/

EE_OBJS := $(EE_OBJS:%=$(EE_OBJS_DIR)%)

.PHONY: all clean

all: $(EE_BIN)

clean:
	rm -rf $(EE_OBJS_DIR) $(EE_BIN)

$(EE_OBJS_DIR):
	@mkdir -p $@

$(EE_OBJS_DIR)%.o: $(EE_SRC_DIR)%.c | $(EE_OBJS_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(EE_OBJS_DIR)%.o: $(EE_SRC_DIR)%.S | $(EE_OBJS_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
