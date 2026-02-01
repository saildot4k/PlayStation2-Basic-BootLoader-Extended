
<br />
<p align="center">
  <a href="https://israpps.github.io/PlayStation2-Basic-BootLoader/">
    <img src="https://israpps.github.io/PlayStation2-Basic-BootLoader/logo.png" alt="Logo" width="100%" height="auto">
  </a>

  <p align="center">
    A flexible BootLoader for PlayStation 2™ and PSX-DESR
    <br />
  </p>
</p>  

[![Codacy Badge](https://app.codacy.com/project/badge/Grade/4ea4628e3d444807bf5df8430a327c5b)](https://www.codacy.com/gh/israpps/PlayStation2-Basic-BootLoader/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=israpps/PlayStation2-Basic-BootLoader&amp;utm_campaign=Badge_Grade)
[![CI](https://github.com/israpps/PlayStation2-Basic-BootLoader/actions/workflows/CI.yml/badge.svg?branch=main)](https://github.com/israpps/PlayStation2-Basic-BootLoader/actions/workflows/CI.yml)
![GitHub all releases](https://img.shields.io/github/downloads/israpps/PlayStation2-Basic-BootLoader/total?logo=GitHub&logoColor=white)



A simple PS2 (and PSX-DESR) bootloader that handles system init and ELF programs execution (amongst other things)

## Documentation

It is hosted on [github pages](https://israpps.github.io/PlayStation2-Basic-BootLoader/)

## Config and arguments

Edit `release/SYS-CONF/PS2BBL.INI` (PS2) or `release/SYS-CONF/PSXBBL.INI` (PSX) and set values to `0` or `1`.

### Visual game ID
- `APP_GAMEID = 1` enables visual game ID for apps/homebrew.
- `CDROM_DISABLE_GAMEID = 1` disables visual game ID for discs launched via `$CDVD`.

### PS1DRV options (PS1 discs only)
These apply only when launching a PS1 disc via `$CDVD` or `$CDVD_NO_LOGO`.
- `PS1DRV_ENABLE_FAST = 1` enables fast PS1 disc speed.
- `PS1DRV_ENABLE_SMOOTH = 1` enables texture smoothing.
- `PS1DRV_USE_PS1VN = 1` runs PS1DRV via PS1VModeNegator.

### App arguments
Use `ARG_<BUTTON>_E? =` lines to pass args to an ELF (see INI examples).
- `-titleid=SLUS_123.45` overrides the app title ID (up to 11 chars).
- `-appid` forces app visual game ID even if `APP_GAMEID = 0`.
You can pass up to 8 args per entry.

### Hotkey names
Use `NAME_<BUTTON> =` to set the label displayed for a hotkey when `LOGO_DISPLAY = 3` (banner + names).
Example:
```
NAME_SQUARE = POPSLOADER
```

### Build flag
- `EMBED_PS1VN=1` (default) embeds PS1VModeNegator; set `0` to load external PS1VN.ELF.

## Known bugs/issues

you tell me ;)

## Credits

- thanks to @SP193 for the OSD initialization libraries, wich serve as the foundation for this project
- thanks asmblur, for encouraging me to make this monster on latest sdk
- thanks to @uyjulian and @fjtrujy for always helping me
