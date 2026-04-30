#ifndef COMMONDEF
#define COMMONDEF

#include "key_count.h"

#define CONFIG_KEY_INDEXES 10       // number of paths to scan per hotkey, however the list below will need to match this value
#define MAX_LEN     64              // max length for hotkey display names (after formatting)
#define CNF_LEN_MAX 20480           // 20kb should be enough for massive CNF's
#define LOGO_DISPLAY_DEFAULT 3      // Default LOGO_DISPLAY when no config is found: 0=off, 1=info, 2=logo+info, 3=banner+NAME
#define DEFDELAY 30000              // default ammount of time this program will wait for a key press in ms/
#define CDROM_DISABLE_GAMEID_DEFAULT 0
#define DISC_STOP_DEFAULT 0
#ifdef PSX
#define APP_GAMEID_DEFAULT 0
#else
#define APP_GAMEID_DEFAULT 1
#endif

typedef enum
{
    SOURCE_MC0 = 0,
    SOURCE_MC1,
    SOURCE_MASS,
#ifdef MX4SIO
    SOURCE_MX4SIO,
#endif
#ifdef HDD
    SOURCE_HDD,
#endif
#ifdef XFROM
    SOURCE_XFROM,
#endif
#ifdef MMCE
    SOURCE_MMCE0,
    SOURCE_MMCE1,
#endif
#ifdef PSX
    SOURCE_XCONFIG,
#endif
    SOURCE_CWD,
    SOURCE_INVALID,
    SOURCE_COUNT,
} CONFIG_SOURCES_ID;

extern char *CONFIG_PATHS[SOURCE_COUNT];
extern const char *SOURCES[SOURCE_COUNT];

typedef enum
{
    AUTO,
    SELECT,
    L3,
    R3,
    START,
    UP,
    RIGHT,
    DOWN,
    LEFT,
    L2,
    R2,
    L1,
    R1,
    TRIANGLE,
    CIRCLE,
    CROSS,
    SQUARE
} KEYS;

extern const char *KEYS_ID[KEY_COUNT];

typedef struct
{
    int key_index;
    int entry_index; // Zero-based CONFIG_KEY_INDEXES slot.
    const char *arg;
} DefaultLaunchArgEntry;

#ifdef PSX
extern const char *DEFAULT_KEYNAMES_PSX[KEY_COUNT];
#endif
extern const char *DEFAULT_KEYNAMES_PS2[KEY_COUNT];

#if defined(PSX)
#define DEFAULT_KEYNAMES DEFAULT_KEYNAMES_PSX
#else
#define DEFAULT_KEYNAMES DEFAULT_KEYNAMES_PS2
#endif

#ifdef PSX
extern char *DEFPATH_PSX[];
extern const DefaultLaunchArgEntry DEFAULT_LAUNCH_ARGS_PSX[];
extern const int DEFAULT_LAUNCH_ARGS_PSX_COUNT;
#endif
extern char *DEFPATH_PS2[];
extern const DefaultLaunchArgEntry DEFAULT_LAUNCH_ARGS_PS2[];
extern const int DEFAULT_LAUNCH_ARGS_PS2_COUNT;

#if defined(PSX)
#define DEFPATH DEFPATH_PSX
#define DEFAULT_LAUNCH_ARGS DEFAULT_LAUNCH_ARGS_PSX
#define DEFAULT_LAUNCH_ARGS_COUNT DEFAULT_LAUNCH_ARGS_PSX_COUNT
#else
#define DEFPATH DEFPATH_PS2
#define DEFAULT_LAUNCH_ARGS DEFAULT_LAUNCH_ARGS_PS2
#define DEFAULT_LAUNCH_ARGS_COUNT DEFAULT_LAUNCH_ARGS_PS2_COUNT
#endif

#ifndef COMMIT_HASH
#define COMMIT_HASH "UNKNOWn"
#endif

#endif // COMMONDEF
