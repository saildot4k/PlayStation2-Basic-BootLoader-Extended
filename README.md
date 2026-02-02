
<br />
<p align="center">
  <a href="https://israpps.github.io/PlayStation2-Basic-BootLoader/">
    <img src="https://github.com/saildot4k/PlayStation2-Basic-BootLoader-Extended/blob/main/assets/ps2bble-logo.png" alt="Logo" width="100%" height="auto">
  </a>

  <p align="center">
    A flexible BootLoader for PlayStation 2™ and PSX-DESR
    <br />
  </p>
</p>  


This is a fork of PS2BBL by [El Isra](https://github.com/israpps/PlayStation2-Basic-BootLoader/)


## Documentation

It is hosted on [github pages](https://israpps.github.io/PlayStation2-Basic-BootLoader/) but with the additions below:

## Config and arguments

Edit `SYS-CONF/PS2BBL.INI` (PS2) or `SYS-CONF/PSXBBL.INI` (PSX) as needed. Paths are alredy pre-set to align with downloads from [PS2 Homebrw Store](https://ps2homebrewstore.com) This means you should only need to edit your Auto choice and best to set args for NHDDL as needed for the device you are loading ISOS from.

### Retrogem Visual game ID
- `APP_GAMEID = 1` enables visual game ID for apps/homebrew up to 11 characters derived from filename or PS1/2 Disc
- `CDROM_DISABLE_GAMEID = 1` disables visual game ID for discs launched via `$CDVD`.

### PS1DRV options (PS1 discs only)
These apply only when launching a PS1 disc via `$CDVD` or `$CDVD_NO_LOGO`.
- `PS1DRV_ENABLE_FAST = 1` enables fast PS1 disc speed.
- `PS1DRV_ENABLE_SMOOTH = 1` enables texture smoothing.
- `PS1DRV_USE_PS1VN = 1` runs PS1DRV via PS1VModeNegator.

### App arguments
Use `ARG_<BUTTON>_E? =` lines to pass up to 8 args to an ELF (see INI examples).
- `-titleid=SLUS_123.45` overrides the app title ID (up to 11 chars).
- `-appid` forces app visual game ID even if `APP_GAMEID = 0`.
You can pass up to 8 args per entry.
Example:
```
NAME_R1 = NHDDL
LK_R1_E1 = mmce?:/NEUTRINO/nhddl.elf
ARG_R1_E1 = -video=480p
ARG_R1_E1 = -mode=mmce
ARG_R1_E1 = -mode=ata
```

### Hotkey names
Use `LOGO_DISPLAY = 3` or greater for hotkey names. Names will be defined by NAME_<BUTTON> or file/path
Use `NAME_<BUTTON> =` to set the label displayed for a hotkey when `LOGO_DISPLAY = 3` (banner + names).
Example:
```
NAME_SQUARE = POPSLOADER
```


## Known bugs/issues

you tell me ;)

## Credits & Thanks

- From saildot4k
  - @pcm720 for his Retrogem gameid and PS1 Video Negator code from [OSDMenu](https://github.com/pcm720/OSDMenu)
  - @sp193 for [PS1 Video Negator](https://github.com/ps2homebrew/PS1VModeNeg)
  - @nathanneurotic for PS2BBLE 10path, ideas and logo
  - @israpps for [PS2BBL](https://github.com/israpps/PlayStation2-Basic-BootLoader/)

- From [El Isra](https://israpps.github.io/) (PS2BBL Developer): 
  - @SP193 for the OSD initialization libraries, wich serve as the foundation for this project
  - @asmblur, for encouraging me to make this monster on latest sdk
  - @uyjulian and @fjtrujy for always helping me

