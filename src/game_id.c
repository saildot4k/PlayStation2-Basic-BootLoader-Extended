#include "game_id.h"

#include "OSDHistory.h"
#include "debugprintf.h"
#include "game_id_table.h"

#include <ctype.h>
#include <dmaKit.h>
#include <gsKit.h>
#include <kernel.h>
#include <libcdvd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int s_app_gameid = 0;
static int s_cdrom_disable_gameid = 0;

void GameIDSetConfig(int app_gameid, int cdrom_disable_gameid)
{
    s_app_gameid = (app_gameid != 0);
    s_cdrom_disable_gameid = (cdrom_disable_gameid != 0);
}

int GameIDAppEnabled(void)
{
    return s_app_gameid;
}

int GameIDDiscEnabled(void)
{
    return !s_cdrom_disable_gameid;
}

static uint8_t calculateCRC(const uint8_t *data, int len)
{
    uint8_t crc = 0x00;
    int i;
    for (i = 0; i < len; i++) {
        crc += data[i];
    }
    return (uint8_t)(0x100 - crc);
}

void gsDisplayGameID(const char *gameID)
{
    if (gameID == NULL || gameID[0] == '\0')
        return;

    GSGLOBAL *gsGlobal = gsKit_init_global();
    gsGlobal->DoubleBuffering = GS_SETTING_ON;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

    if (dmaKit_chan_init(DMA_CHANNEL_GIF))
        return;

    gsKit_init_screen(gsGlobal);
    gsKit_display_buffer(gsGlobal);
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);
    gsKit_clear(gsGlobal, GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x00));

    uint8_t data[64] = {0};
    int gidlen = (int)strnlen(gameID, 11);

    int dpos = 0;
    data[dpos++] = 0xA5;
    data[dpos++] = 0x00;
    dpos++;
    data[dpos++] = (uint8_t)gidlen;

    memcpy(&data[dpos], gameID, gidlen);
    dpos += gidlen;

    data[dpos++] = 0x00;
    data[dpos++] = 0xD5;
    data[dpos++] = 0x00;

    int data_len = dpos;
    data[2] = calculateCRC(&data[3], data_len - 3);

    int xstart = (gsGlobal->Width / 2) - (data_len * 8);
    int ystart = gsGlobal->Height - (((gsGlobal->Height / 8) * 2) + 20);
    int height = 2;

    int i, j;
    for (i = 0; i < data_len; i++) {
        for (j = 7; j >= 0; j--) {
            int x = xstart + (i * 16 + ((7 - j) * 2));
            int x1 = x + 1;
            gsKit_prim_sprite(gsGlobal, x, ystart, x1, ystart + height, 0, GS_SETREG_RGBA(0xFF, 0x00, 0xFF, 0x00));

            uint32_t color = ((data[i] >> j) & 1) ? GS_SETREG_RGBA(0x00, 0xFF, 0xFF, 0x00) : GS_SETREG_RGBA(0xFF, 0xFF, 0x00, 0x00);
            gsKit_prim_sprite(gsGlobal, x1, ystart, x1 + 1, ystart + height, 0, color);
        }
    }

    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    gsKit_sync_flip(gsGlobal);
    gsKit_deinit_global(gsGlobal);
}

int validateTitleID(const char *titleID)
{
    if (titleID == NULL)
        return 0;
    if ((titleID[4] == '_') && ((titleID[7] == '.') || (titleID[8] == '.'))) {
        return 1;
    }
    return 0;
}

void GameIDHandleDisc(const char *titleID, int display)
{
    if (titleID == NULL || titleID[0] == '\0')
        return;

    if (validateTitleID(titleID))
        UpdatePlayHistory(titleID);

    if (display && titleID[0] != '?')
        gsDisplayGameID(titleID);
}

void GameIDHandleApp(const char *titleID, int showAppID)
{
    if (!showAppID || titleID == NULL || titleID[0] == '\0')
        return;

    if (!strncmp(titleID, "SCPN", 4) || !validateTitleID(titleID)) {
        gsDisplayGameID(titleID);
        return;
    }

    UpdatePlayHistory(titleID);
    gsDisplayGameID(titleID);
}

const char *getPS1GenericTitleID(void)
{
    char sectorData[2048] = {0};
    sceCdRMode mode = {
        .trycount = 3,
        .spindlctrl = SCECdSpinNom,
        .datapattern = SCECdSecS2048,
    };

    if (!sceCdRead(16, 1, &sectorData, &mode) || sceCdSync(0)) {
        DPRINTF("Failed to read PVD\n");
        return NULL;
    }

    if (strncmp(&sectorData[1], "CD001", 5)) {
        DPRINTF("Invalid PVD\n");
        return NULL;
    }

    size_t i;
    for (i = 0; i < gameIDTableCount; ++i) {
        if (strncmp(&sectorData[0x32D], gameIDTable[i].volumeTimestamp, 16) == 0) {
            return gameIDTable[i].gameID;
        }
    }
    return NULL;
}

char *generateTitleID(const char *path)
{
    if (!path)
        return NULL;

    char *titleID = calloc(sizeof(char), 12);
    if (!titleID)
        return NULL;
    const char *valuePtr = NULL;

    if (!strncmp(path, "cdrom", 5)) {
        valuePtr = strchr(path, '\\');
        if (!valuePtr) {
            valuePtr = strchr(path, ':');
            if (!valuePtr) {
                DPRINTF("Failed to parse the executable for the title ID\n");
                goto fallback;
            }
        }
        valuePtr++;

        if ((strlen(valuePtr) >= 11) && (valuePtr[4] == '_') && ((valuePtr[7] == '.') || (valuePtr[8] == '.'))) {
            strncpy(titleID, valuePtr, 11);
            return titleID;
        } else {
            const char *tID = getPS1GenericTitleID();
            if (tID) {
                DPRINTF("Guessed the title ID from disc PVD: %s\n", tID);
                strncpy(titleID, tID, 11);
            }
            return titleID;
        }
    }

    if (!strncmp(path, "hdd0:", 5)) {
        valuePtr = &path[5];

        if (valuePtr[1] == 'P' && valuePtr[2] == '.') {
            valuePtr += 3;
            strncpy(titleID, valuePtr, 11);

            char *dot = strchr(titleID, '.');
            char *col = strchr(titleID, ':');
            if (dot || col) {
                char *cut = dot ? dot : col;
                *cut = '\0';
            }

            if (titleID[4] == '-') {
                int i;
                for (i = 5; i < 10; i++) {
                    if ((titleID[i] < '0') || (titleID[i] > '9'))
                        goto whitespace;
                }

                titleID[4] = '_';
                titleID[10] = titleID[9];
                titleID[9] = titleID[8];
                titleID[8] = '.';
                DPRINTF("Got the title ID from partition name: %s\n", titleID);
                return titleID;
            }
            goto whitespace;
        }
    }

fallback:
    DPRINTF("Extracting title ID from ELF name: %s\n", titleID);
    const char *ext = strstr(path, ".ELF");
    if (!ext)
        ext = strstr(path, ".elf");

    const char *elfName = strrchr(path, '/');
    if (!elfName) {
        free(titleID);
        return NULL;
    }

    elfName++;
    char *tmpPath = NULL;
    if (ext) {
        tmpPath = strdup(path);
        if (tmpPath) {
            char *tmpExt = strstr(tmpPath, ".ELF");
            if (!tmpExt)
                tmpExt = strstr(tmpPath, ".elf");
            if (tmpExt)
                *tmpExt = '\0';
            elfName = strrchr(tmpPath, '/');
            if (elfName)
                elfName++;
        }
    }

    strncpy(titleID, elfName ? elfName : path, 11);
    if (tmpPath)
        free(tmpPath);

whitespace:
    {
        int i;
        for (i = 10; i >= 0; i--) {
            if (isspace((int)titleID[i])) {
                titleID[i] = '\0';
                break;
            }
        }
    }
    DPRINTF("Path: %s\nTitle ID: %s\n", path, titleID);
    return titleID;
}
