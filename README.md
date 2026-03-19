
<br />
<p align="center">
  <a href="https://israpps.github.io/PlayStation2-Basic-BootLoader/">
    <img src="https://github.com/saildot4k/PlayStation2-Basic-BootLoader-Extended/blob/main/assets/ps2bbl_example.png" alt="Logo" width="50%" height="auto">
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
  - DECKARD IE 75K and later: Logo Patches are applied via PS2SDK XPARAM.IRX
- ~~`SKIP_PS2LOGO`~~ Global config is deprecated from PS2BBLE because the above 2 options cover with and wthout logo.

### Video Mode
- `VIDEO_MODE = AUTO, NTSC, PAL, 480P` Will use PS2 default or force either of the other 3 modes.

#### Emergency video mode selector
- Enter with `TRIANGLE + CROSS` during boot
- Selector forces `LOGO_DISPLAY = 5` while active so you can see logo/hotkey UI context.
- Controls:
  - `LEFT/RIGHT` cycle modes: `AUTO`, `NTSC`, `PAL`, `480P`
  - `SELECT` save the currently selected mode to the active config file
  - `START` exit selector and continue to hotkey display with first found full path

### PS1DRV options (PS1 discs only)
These apply only when launching a PS1 disc via `$CDVD` or `$CDVD_NO_PS2LOGO`.
- `PS1DRV_ENABLE_FAST = 1` enables fast PS1 disc speed.
- `PS1DRV_ENABLE_SMOOTH = 1` enables texture smoothing.
- `PS1DRV_USE_PS1VN = 1` runs PS1DRV via PS1VModeNegator.

### App arguments
Use `ARG_<BUTTON>_E? =` lines to pass up to 8 args to an ELF (see INI examples).
- `-titleid=SLUS_123.45` overrides the app title ID (up to 11 chars).
- `-appid` forces app visual game ID even if `APP_GAMEID = 0`.
- `-dev9=<mode>` sets DEV9/HDD policy before launching the target ELF. Supported modes:
  - `NICHDD` keeps both DEV9 (network adapter) and HDD powered/on.
  - `NIC` keeps DEV9/network on, unmounts `pfs0:`, and puts `hdd0:`/`hdd1:` into immediate idle.
  - if omitted, PS2BBL does not force a DEV9 policy override.
  - note: on non-HDD builds this option has no effect.
- `-patinfo` enables PATINFO handling: if launch path contains `:PATINFO`, the first remaining arg is used as target ELF path.
  This is mainly for HDD builds.
- `-la=<flags>` is reserved for the internal stage2 loader and is ignored if provided by user config.
You can pass up to 8 args per entry. Args are processed in the same order they are written in the INI.
Example:
```
NAME_R1 = NHDDL
LK_R1_E1 = mmce?:/NEUTRINO/nhddl.elf
ARG_R1_E1 = -video=480p
ARG_R1_E1 = -mode=mmce
ARG_R1_E1 = -mode=ata
```

#### Argument precedence and order
1. PS2BBL first parses and consumes loader-control args from `ARG_*`: `-appid`, `-titleid=`, `-dev9=`, `-patinfo`, `-gsm=`.
2. For repeated control args, the last valid value wins (`-dev9`, `-gsm`). Invalid `-gsm` values are ignored and do not clear a prior valid one.
3. If `-patinfo` is set and launch path contains `:PATINFO`, the first remaining app argument becomes the target ELF path and is removed from app argv.
4. Remaining arguments preserve order and are passed to the launched app.
5. If eGSM is active, stage2 handoff appends internal args in OSDMenu-style order: `[..., <gsm-value>, -la=G|GN|GD]`.
6. User-provided `-la=` is always ignored (reserved for internal loader control).

### Hotkey names
Use `LOGO_DISPLAY = <value>` 3 or greater for hotkey names. Names will be defined by NAME_<BUTTON> or file/path
  - `0` No Logo/Console info
  - `1` Console Info
  - `2` PS2BBLE/PSXBBLE Logo and Console Info
  - `3` Hotkey Graphic Display with `NAME_BUTTON = <TITLE>` displayed from config file
  - `4` Hotkey Graphic Display with first found file as defined in config
  - `5` Hotkey Graphic Display with first found file path as defined in config
Use `NAME_<BUTTON> =` to set the label displayed for a hotkey when `LOGO_DISPLAY = 3` (banner + names).
Example:
```
NAME_SQUARE = POPSLOADER
```

### Custom splash logo from CWD
If a custom logo file is found in the current working directory, it replaces the embedded PS2BBLE/PSXBBLE logo.

- Filename: `LOGO.BIN` (uppercase only)
- Required dimensions: `256 x 64`
- Preferred format: indexed `LGB1` bin (8-bit pixel indices + RBGA palette)
- Palette size: up to 255 colors by default (`--max-colors`), max supported 256
- Legacy format is still accepted: raw headerless `RBGA` (`R, B, G, A`) at `65536` bytes
- Runtime PNG decoding is intentionally not used, to avoid extra boot-time code size and CPU cost

Convert a PNG to the expected raw file:

```bash
python tools/png_to_logo_rbga.py assets/my_logo.png -o LOGO.BIN --width 256 --height 64 --max-colors 255
```

Then place `LOGO.BIN` in the same CWD where PS2BBL resolves `CONFIG.INI`.

### eGSM (external Graphics Synthesize Mode)
For PS2 discs, eGSM is read from `OSDGSM.CNF` automatically (no INI path setting required). See [here](https://github.com/pcm720/OSDMenu/blob/main/patcher/README.md#osdgsmcnf) for config file format.

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

eGSM ARG examples:
- `-gsm=fp2`
- `-gsm=fp2:1`
- `-gsm=1080ix2`

Usage example:
```
LK_TRIANGLE_E1 = mc0:/APP_WLE-ISR/WLE-ISR.ELF
ARG_TRIANGLE = -gsm=1080ix2
```

### PATINFO example:
```
LK_AUTO_E1 = hdd0:+OSDMENU:PATINFO
ARG_AUTO_E1 = -patinfo
ARG_AUTO_E1 = -gsm=fp2:1
ARG_AUTO_E1 = -dev9=NIC
ARG_AUTO_E1 = pfs:/APPS/APP.ELF
```

## Known bugs/issues

you tell me ;)

## Credits & Thanks

- From saildot4k
  - @israpps for [PS2BBL](https://github.com/israpps/PlayStation2-Basic-BootLoader/)
  - @pcm720 for
    - Retrogem gameID, PS1 Video Negator, eGSM,  PS2LOGO code from [OSDMenu](https://github.com/pcm720/OSDMenu)
    - Video mode options from [NHDDL](https://github.com/pcm720/nhddl)
  - @sp193 for [PS1 Video Negator](https://github.com/ps2homebrew/PS1VModeNeg)
  - @nathanneurotic for PS2BBLE 10path, ideas and persuasion
  - [@Berion](https://www.psx-place.com/members/berion.1431/) for logos, background and hotkey graphics

- From [El Isra](https://israpps.github.io/) (PS2BBL Developer): 
  - @SP193 for the OSD initialization libraries, wich serve as the foundation for this project
  - @asmblur, for encouraging me to make this monster on latest sdk
  - @uyjulian and @fjtrujy for always helping me

