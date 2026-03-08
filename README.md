
<br />
<p align="center">
  <a href="https://israpps.github.io/PlayStation2-Basic-BootLoader/">
    <img src="https://github.com/saildot4k/PlayStation2-Basic-BootLoader-Extended/blob/main/assets/embedded/logo_ps2bble.png" alt="Logo" width="100%" height="auto">
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

### PS2LOGO patching (PS2 discs only)
- `$CDVD` Runs disc with logo.  PS2BBL gets the target video mode from the disc's SYSTEM.CNF and patches PS2LOGO to always use the disc region instead of the console region, removing logo checksum check.
- `$CDVD_NO_PS2LOGO` always boots PS2 discs directly (no logo).
- ~~`SKIP_PS2LOGO`~~ Global config is deprecated from PS2BBL because the above 2 options cover with and wthout logo.

### Loader video mode (PS2BBL/PSXBBL UI only)
- `VIDEO_MODE = AUTO` (default if missing) uses the console native mode from ROM region.
- `VIDEO_MODE = NTSC` forces PS2BBL/PSXBBL runtime UI to NTSC.
- `VIDEO_MODE = PAL` forces PS2BBL/PSXBBL runtime UI to PAL.
- `VIDEO_MODE = 480p` forces PS2BBL/PSXBBL runtime UI to 480p progressive scan.
- Value matching is case-insensitive (`480p` and `480P` are equivalent).
- This setting is not forwarded to launched apps/discs; PS2BBL restores native mode before handoff.

### PS1DRV options (PS1 discs only)
These apply only when launching a PS1 disc via `$CDVD` or `$CDVD_NO_PS2LOGO`.
- `PS1DRV_ENABLE_FAST = 1` enables fast PS1 disc speed.
- `PS1DRV_ENABLE_SMOOTH = 1` enables texture smoothing.
- `PS1DRV_USE_PS1VN = 1` runs PS1DRV via PS1VModeNegator.

### App arguments
Use `ARG_<BUTTON>_E? =` lines to pass up to 8 args to an ELF (see INI examples).
- `-titleid=SLUS_123.45` overrides the app title ID (up to 11 chars).
- `-appid` forces app visual game ID even if `APP_GAMEID = 0`.
- `-dev9=<mode>` sets DEV9/HDD policy before launching the target ELF.
- supported values:
  - `NICHDD` keeps both DEV9 (network adapter) and HDD powered/on.
  - `NIC` keeps DEV9/network on, unmounts `pfs0:`, and puts `hdd0:`/`hdd1:` into immediate idle.
  - if omitted, PS2BBL does not force a DEV9 policy override.
  - note: on non-HDD builds this option has no effect.
- `-patinfo` enables PATINFO handling: if launch path contains `:PATINFO`, the first remaining arg is used as target ELF path.
  This is mainly for HDD builds.
You can pass up to 8 args per entry. Args are processed in the same order they are written in the INI.
Example:
```
NAME_R1 = NHDDL
LK_R1_E1 = mmce?:/NEUTRINO/nhddl.elf
ARG_R1_E1 = -video=480p
ARG_R1_E1 = -mode=mmce
ARG_R1_E1 = -mode=ata
```
__eGSM NOT YET IMPLEMENTED__
- `-gsm=<v[:c]>` runs the target ELF via embedded eGSM (ignored for `rom?:` paths).
  - eGSM is applied to the launched target (ELF/disc), not to PS2BBL itself.
  eGSM format (OSDMenu-style):
- `v` = video mode:
- empty = do not force (default)
- `fp1` = force progressive scan (240p/288p)
- `fp2` = force progressive scan (480p/576p)
- `1080ix1` = force 1080i, width/height x1
- `1080ix2` = force 1080i, width/height x2
- `1080ix3` = force 1080i, width/height x3
- `c` = compatibility mode:
- empty = none (default)
- `1` = field flipping type 1
- `2` = field flipping type 2
- `3` = field flipping type 3

eGSM examples:
- `-gsm=fp2`
- `-gsm=fp2:1`
- `-gsm=1080ix2`

For PS2 discs, eGSM is read from `OSDGSM.CNF` automatically (no INI path setting required).
PATINFO example:
```
LK_AUTO_E1 = hdd0:+OSDMENU:PATINFO
ARG_AUTO_E1 = -patinfo
ARG_AUTO_E1 = -gsm=fp2:1
ARG_AUTO_E1 = -dev9=NIC
ARG_AUTO_E1 = pfs:/APPS/APP.ELF
```

### Hotkey names
Use `LOGO_DISPLAY = 3` or greater for hotkey names. Names will be defined by NAME_<BUTTON> or file/path
Use `NAME_<BUTTON> =` to set the label displayed for a hotkey when `LOGO_DISPLAY = 3` (banner + names).
Example:
```
NAME_SQUARE = POPSLOADER
```


## Known bugs/issues

- Master Patched Disc logos may be inverted. No fix incmoning - PCM720
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

