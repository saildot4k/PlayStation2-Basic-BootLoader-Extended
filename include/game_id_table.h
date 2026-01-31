#ifndef GAME_ID_TABLE_H
#define GAME_ID_TABLE_H

#include <stddef.h>

struct GameIDEntry {
    const char *volumeTimestamp;
    const char *gameID;
};

extern const struct GameIDEntry gameIDTable[];
extern const size_t gameIDTableCount;

#endif
