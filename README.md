
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

Edit config file as as needed. Recommended to [R3Configurator](https://github.com/saildot4k/R3CONFIGURATOR) to edit your config file. Paths are alredy pre-set to align with downloads from [PS2 Homebrw Store](https://ps2homebrewstore.com) This means you should only need to edit your Auto choice and best to set args for NHDDL as needed for the device you are loading ISOS from.

### Config Path search order

Config search is now boot-family aware to avoid loading unnecessary drivers at startup:

1. `CONFIG.INI` (CWD: Current Working Directory)
2. When loaded from device:
   - MMCE boot: `mmce?:/PS2BBL/CONFIG.INI`
   - MX4SIO boot: `mx4sio:/PS2BBL/CONFIG.INI`
   - APA HDD boot: `hdd0:__sysconf:pfs:/PS2BBL/CONFIG.INI`
   - BDM boot: `usb:/PS2BBL/CONFIG.INI` / `mass:/PS2BBL/CONFIG.INI` / `ata:/PS2BBL/CONFIG.INI`
3. PSX DESR Specific
   - `xfrom:/PS2BBL/CONFIG.INI`
4. Memory card fallback:
   - `mc?:/SYS-CONF/PS2BBL.INI`
   - `mc?:/SYS-CONF/PSXBBL.INI` _PSX Specific_
5. When compiled with `DISC_STOP_AT_BOOT=11
   - `cdrom0:/PS2BBL/CONFIG.INI`

### Device path prefixes
PS2BBL supports these launch/config path prefixes:
- `mc0:/`, `mc1:/`, `mc?:/`
- `usb:/` (preferred), and `mass:/` (legacy)

#### Build Specific:  
- `mmce0:/`, `mmce1:/`, `mmce?:/` __MMCE builds__
- `mx4sio:/` (preferred), and `massX:/` (legacy)  __MX4SIO builds__
- `hdd0:partition:pfs:/<path to elf>` __HDD builds__
- `xfrom:/` paths __PSX DESR builds__
- `ata:/`, `ilink:/` (BDM mass-storage roots) __not yet implemented__
- `DISC_STOP_AT_BOOT=1` compile-time profile to always stop optical disc after config bootstrap

### LOGO_DISPLAY
Use `LOGO_DISPLAY = 3` for hotkey-name display. Names will be defined by `NAME_<BUTTON>`.
  - `0` No Logo/Console info
  - `1` Console Info
  - `2` PS2BBLE/PSXBBLE Logo and Console Info
  - `3` Hotkey Graphic Display with `NAME_BUTTON = <TITLE>` displayed from config file
    - Example:
      ```
      NAME_SQUARE = POPSLOADER
      ```

#### Custom splash logo from CWD
If a custom logo file (LOGO.BIN) is found in CWD (current working directory), it replaces the embedded PS2BBLE/PSXBBLE logo.

Convert a PNG to the expected raw file:
  - PNG should be RGBA 8 Bit indexed 255 colors per channel and 256x64 resolution.

  - Release package (script is in release root):
  ```bash
  python png_to_logo_rbga.py my_logo.png
  ```

  - Place the created `LOGO.BIN` next to PS2BBL Extended. This should be `mc?:/BOOT/` if using downloads from [PS2 Homebrew Store](https://ps2store.com)

### Retrogem Visual game ID
- `APP_GAMEID = 1` enables visual game ID for apps/homebrew up to 11 characters derived from filename or PS1/2 Disc
- `CDROM_DISABLE_GAMEID = 1` disables visual game ID for discs launched via `cdrom`.

### Disc spin control
- Global key/value: `DISC_STOP = 0/1`
- `DISC_STOP = 1` stops optical disc after config bootstrap when a config file was found.
- `DISC_STOP = 0` (or omitted) keeps default behavior.
- `-disc_stop` can be added per launch entry to stop disc after target ELF is found.

### Video Mode
- `VIDEO_MODE = AUTO, NTSC, PAL, 480P` Will use PS2 default or force either of the other 3 modes.

#### Emergency video mode selector
- Enter with `TRIANGLE + CROSS` during boot
- Selector forces `LOGO_DISPLAY = 3` while active so you can see logo/hotkey UI context.
- Controls:
  - `LEFT/RIGHT` cycle modes: `AUTO`, `NTSC`, `PAL`, `480P`
  - `SELECT` save the currently selected mode to the active config file
  - `START` exit selector and continue to hotkey display

### CDROM launch args
For `LK_*_E* = cdrom`, supported args are:
- `-nologo`
- `-nogameid`
- `-ps1fast`
- `-ps1smooth`
- `-ps1vneg`
- `-gsm=<v[:c]>`

#### PS2LOGO arguments (PS2 discs only)
- Use `cdrom` as the launch entry path.
- Add `-nologo` to the entry args to boot PS2 discs directly (skip `rom0:PS2LOGO`).
- Without `-nologo`, PS2BBL runs disc via PS2LOGO and patches it to always use disc region and bypass logo checksum checks.
- DECKARD IE 75K and later: XPARAM.IRX is applied automatically when needed for no-logo launches.

#### PS1 disc arguments (per launch)
These apply per `cdrom` launch entry, not globally.
- `-ps1fast` enables fast PS1 disc speed.
- `-ps1smooth` enables texture smoothing.
- `-ps1vneg` runs PS1DRV via PS1VModeNegator. Useful on setups where console mode and disc mode differ (for example some MechaPwn/modchip cases).

### App arguments
Use `ARG_<BUTTON>_E? =` lines to pass args to an ELF (see INI examples).
- Insert launched elf args first then append with desired internal PS2BBL args next (see below) [NHDDL](https://github.com/pcm720/nhddl) is a great candidate to use args, as it speeds up NHDDL boot.
- `-titleid=SLUS_123.45` overrides the app title ID (up to 11 chars).
- `-appid` forces app visual game ID even if `APP_GAMEID = 0`.
- `-disc_stop` stops disc after target ELF is loaded (useful per entry without `DISC_STOP_AT_BOOT` builds).
- `-gsm=<v[:c]>` runs the target ELF via embedded eGSM (ignored for `rom?:` paths). This must be the last arg if used.
- `-disc_stop` is ignored for `cdrom` launch entries.
- `-dev9=<mode>` sets DEV9/HDD policy before launching the target ELF. Supported modes:
  - `NICHDD` keeps both DEV9 (network adapter) and HDD powered/on.
  - `NIC` keeps DEV9/network on, unmounts `pfs0:`, and puts `hdd0:`/`hdd1:` into immediate idle.
  - if omitted, PS2BBL does not force a DEV9 policy override.
  - note: on non-HDD builds this option has no effect.
- `-patinfo` enables PATINFO handling for `:PATINFO` entries.
  - PS2BBL reads `SYSTEM.CNF` from the partition attribute area (`PS2ICON3D`) and applies `BOOT2/BOOT/path`, `arg*`, `skip_argv0`, and `HDDUNITPOWER`.
  - if `BOOT2/BOOT/path=PATINFO`, PS2BBL loads the embedded ELF from partition attribute area and uses internal `-la=E...` handoff.
  - if `IOPRP=PATINFO` (or custom path), PS2BBL passes it to stage2 via internal `-la=I...` handoff.
  - if `-patinfo` is present, the first remaining arg overrides the CNF `BOOT2/BOOT/path` target.
  This is for HDD builds.  
  PATINFO example:  
    ```
    NAME_R1 = My App via PATINFO
    LK_R1_E1 = hdd0:+OSDMENU:PATINFO
    ARG_R1_E1 = pfs:/APPS/MYAPP.ELF
    ARG_R1_E1 = -patinfo               # Tells PS2BBL to use first arg as patinfo boot path.
    ```

Example to launch NHDDL with video mode 480p and look for isos on mmce and exfat hdd without needing nhddl.yaml. The benefit is no wasted time loading drivers, finding and loading nhddl.yaml. This is the quickest way to boot NHDDL and show ISO list.
```
NAME_R1 = NHDDL
LK_R1_E1 = mmce?:/NEUTRINO/nhddl.elf
ARG_R1_E1 = -video=480p
ARG_R1_E1 = -mode=mmce
ARG_R1_E1 = -mode=ata
```

#### Argument precedence and order
1. PS2BBL parses and consumes only trailing loader-control args from `ARG_*`: `-appid`, `-titleid=`, `-dev9=`, `-patinfo`, `-disc_stop`, `-gsm=`.
2. Parsing is bottom to top and stops at the first non-control arg (OSDMenu-style trailing behavior).
4. For `:PATINFO` launch paths, PS2BBL parses partition-attribute `SYSTEM.CNF` and appends CNF `arg*` entries after user app arguments.
5. If `-patinfo` is set and launch path contains `:PATINFO`, the first remaining user app argument becomes the target ELF path (overrides CNF boot path) and is removed from app argv. If no remaining argument exists, launch is aborted (OSDMenu parity), and CNF `BOOT2/BOOT/path` is not used.
6. Remaining arguments preserve order and are passed to the launched app.


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

## Deprecated/Removed
- ~~`RUNKELF`~~ PS2BBLE now handles launching elf/kelf internally without user needint to define kelf launch action.


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

