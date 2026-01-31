#ifndef GAME_ID_H
#define GAME_ID_H

void GameIDSetConfig(int app_gameid, int cdrom_disable_gameid);
int GameIDAppEnabled(void);
int GameIDDiscEnabled(void);

void gsDisplayGameID(const char *gameID);
int validateTitleID(const char *titleID);
void GameIDHandleDisc(const char *titleID, int display);
void GameIDHandleApp(const char *titleID, int showAppID);

const char *getPS1GenericTitleID(void);
char *generateTitleID(const char *path);

#endif
