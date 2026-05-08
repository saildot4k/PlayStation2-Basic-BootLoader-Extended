// Controller initialization and unified pad-state polling helpers.
#include <kernel.h>
#include <libpad.h>
#include <stdio.h>
#include <string.h>
#include <tamtypes.h>

#include "debugprintf.h"
#include "pad.h"

static unsigned char padArea[2][256] ALIGNED(64);
static unsigned int old_pad[2] = {0, 0};
static unsigned char analog_mode_ready[2] = {0, 0};
int pad_inited = 0;

static void TryEnableAnalogMode(int port, int slot)
{
    int pad_state;
    int mode_count;
    int i;

    if ((unsigned int)port >= (sizeof(analog_mode_ready) / sizeof(analog_mode_ready[0])))
        return;

    pad_state = padGetState(port, slot);
    if ((pad_state == PAD_STATE_DISCONN) || (pad_state == PAD_STATE_ERROR)) {
        analog_mode_ready[port] = 0;
        return;
    }

    if (analog_mode_ready[port])
        return;

    if ((pad_state != PAD_STATE_STABLE) && (pad_state != PAD_STATE_FINDCTP1))
        return;

    if (padInfoMode(port, slot, PAD_MODECURID, 0) == PAD_TYPE_DUALSHOCK) {
        analog_mode_ready[port] = 1;
        return;
    }

    mode_count = padInfoMode(port, slot, PAD_MODETABLE, -1);
    for (i = 0; i < mode_count; i++) {
        if (padInfoMode(port, slot, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK) {
            if (padSetMainMode(port, slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK) == 1) {
                DPRINTF("%s: port %d requested analog mode\n", __FUNCTION__, port);
            }
            return;
        }
    }

    analog_mode_ready[port] = 1;
}

void PadInitPads(void)
{
    DPRINTF("%s: start\n", __FUNCTION__);
    padInit(0);
    DPRINTF("%s: padPortOpen(0, 0, padArea[0])\n", __FUNCTION__);
    padPortOpen(0, 0, padArea[0]);
    DPRINTF("%s: padPortOpen(1, 0, padArea[1])\n", __FUNCTION__);
    padPortOpen(1, 0, padArea[1]);

    old_pad[0] = 0;
    old_pad[1] = 0;
    analog_mode_ready[0] = 0;
    analog_mode_ready[1] = 0;
    pad_inited = 1;
}

void PadDeinitPads(void)
{
    DPRINTF("%s: ", __func__);
    if (pad_inited) {
        padPortClose(0, 0);
        padPortClose(1, 0);
        padEnd();
        pad_inited = 0;
        DPRINTF("done\n");
    }
}

int PadIsInitialized(void)
{
    return pad_inited;
}

int ReadPadStatus_raw(int port, int slot)
{
    struct padButtonStatus buttons;
    u32 paddata;

    paddata = 0;
    TryEnableAnalogMode(port, slot);
    if (padRead(port, slot, &buttons) != 0) {
        paddata = 0xffff ^ buttons.btns;
    }

    return paddata;
}

int ReadCombinedPadStatus_raw(void)
{
    return (ReadPadStatus_raw(0, 0) | ReadPadStatus_raw(1, 0));
}

int ReadPadStatus(int port, int slot)
{
    struct padButtonStatus buttons;
    u32 new_pad, paddata;

    new_pad = 0;
    TryEnableAnalogMode(port, slot);
    if (padRead(port, slot, &buttons) != 0) {
        paddata = 0xffff ^ buttons.btns;

        new_pad = paddata & ~old_pad[port];
        old_pad[port] = paddata;
    }

    return new_pad;
}

int ReadCombinedPadStatus(void)
{
    return (ReadPadStatus(0, 0) | ReadPadStatus(1, 0));
}
