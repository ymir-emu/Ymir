# Ymir

[![Stable release](https://github.com/ymir-emu/Ymir/actions/workflows/stable-release.yaml/badge.svg)](https://github.com/ymir-emu/Ymir/actions/workflows/stable-release.yaml) [![Nightly release](https://github.com/ymir-emu/Ymir/actions/workflows/nightly-release.yaml/badge.svg)](https://github.com/ymir-emu/Ymir/actions/workflows/nightly-release.yaml) <a href="https://discord.gg/NN3A7n5dzn">![Discord Shield](https://discord.com/api/guilds/1368676375627694341/widget.png?style=shield)</a> <a href="https://patreon.com/StrikerX3">![Patreon Shield](https://img.shields.io/badge/Patreon-F96854?style=flat&logo=patreon&logoColor=white)</a>

A work-in-progress Sega Saturn emulator.

Join the [Discord community](https://discord.gg/NN3A7n5dzn).

Find the official compatibility list [here](https://docs.google.com/spreadsheets/d/1SLZzL9LelSlpEmTKy8cjaQnE7mew2uW1rfCgcekO58Q/edit?usp=sharing).

Grab the latest release: [stable](https://github.com/ymir-emu/Ymir/releases/latest), [nightly](https://github.com/ymir-emu/Ymir/releases/latest-nightly).

> [!IMPORTANT]
> Windows users: install the latest [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vc14/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vc14/vc_redist.arm64.exe)) before running the emulator.
> This is **mandatory** to avoid crashes on startup.
>
> macOS users: follow [these instructions](https://support.apple.com/guide/mac-help/open-a-mac-app-from-an-unknown-developer-mh40616/mac) to allow Ymir to run on your system. Ymir is signed with an ad-hoc certificate, flagging it as unverified.
>
> Linux users: if you installed the Flatpak version, make sure to [grant it permission to access the filesystem](TROUBLESHOOTING.md#game-discs-dont-load-with-the-flatpak-release) or your disc images won't load properly.

<div class="grid" markdown>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/cd-player.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/sonic-r.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/virtua-fighter-2.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/radiant-silvergun.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/panzer-dragoon-saga.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/nights-into-dreams.png"/>
  <img width="100%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/debugger.png"/>
</div>


## Features

- Load games from MAME CHD, BIN+CUE, IMG+CCD, MDF+MDS or ISO files
- Automatic IPL (BIOS) ROM detection
- Automatic region switching
- Up to two players with a variety of controllers on both ports
- Fully customizable keybindings
- Backup RAM, DRAM and ROM cartridges (more to come)
- Integrated backup memory manager to import and export saves, and transfer between internal and cartridge RAM
- Forwards-compatible save states
- Rewinding (up to one minute at 60 fps), turbo speed, frame step (forwards and backwards)
- Full screen mode with VRR support and low input lag
- Graphics enhancements such as optional deinterlaced/progressive rendering of high resolution modes and transparent mesh polygon rendering
- Optional low level CD block emulation
- A work-in-progress feature-rich debugger

Ymir runs on Windows 10 or later, macOS 12 (Monterey) or later, most modern and popular Linux distributions and FreeBSD, and supports x86-64 (Intel, AMD) and ARM CPUs.


## Usage

Grab the latest release [here](https://github.com/ymir-emu/Ymir/releases/latest).
Check the [Releases](https://github.com/ymir-emu/Ymir/releases) page for previous versions.

Ymir does not require installation. Simply download it to any directory and run the executable.
On Windows you also need to install the latest [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vc14/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vc14/vc_redist.arm64.exe)).
It is recommended to install the latest GPU drivers in order to use Direct3D or Vulkan:
- Nvidia: [Find your GPU here](https://www.nvidia.com/Download/index.aspx?lang=en-us) or [use the Nvidia App to update drivers automatically](https://www.nvidia.com/en-us/software/nvidia-app/)
- AMD: [Find your GPU or CPU here](https://www.amd.com/en/support) or use the download buttons at the top to update drivers automatically
- Intel: [Find your GPU or CPU here](https://www.intel.com/content/www/us/en/download-center/home.html)

The program accepts command-line arguments. Invoke `ymir-sdl3 --help` to list the options:

```
Ymir - Sega Saturn emulator
Usage:
  Ymir [OPTION...] path to disc image

  -d, --disc arg      Path to Saturn disc image (.ccd, .chd, .cue, .iso, 
                      .mds)
  -p, --profile arg   Path to profile directory
  -u, --user          Force user profile
  -h, --help          Display help text
  -f, --fullscreen    Start in fullscreen mode
  -P, --paused        Start paused
  -F, --fast-forward  Start in fast-forward mode
  -D, --debug         Start with debug tracing enabled
  -E, --exceptions    Capture all unhandled exceptions

```

The options are case-sensitive -- lowercase `-p` sets the profile path, uppercase `-P` makes the emulator start paused.

Pass a path to a valid Saturn disc image as an argument to `ymir-sdl3` to launch the emulator with the disc. `-d`/`--disc` is optional.

Use `-p <profile-path>` to point to a separate set of configuration and state files, useful if you wish to have different user profiles (hence the name).

The `-u` option forces usage of the OS's user profile folder (e.g. `C:\Users\<username>\AppData\Roaming\StrikerX3\Ymir` on Windows or `/home/<username>/.local/share/Ymir` on Linux).

For VDP2 graphics reverse engineering, start with `--debug`, open **Debug > VDP > Memory write trace**, select VRAM and/or CRAM, and enable capture. The address interval is start-inclusive and end-exclusive. **Export synchronized bundle** writes the bounded trace, VDP2 VRAM, CRAM, registers, and a receipt into the profile's `dumps` directory from one emulator-thread callback. Each row records the VDP1 frame index, mapped VDP2 destination address, write width, and value. A 32-bit bus store that is split into two 16-bit accesses appears as two rows. Writes before the first VDP1 frame have an `unknown` frame index. Resetting the VDP clears the buffered trace so frame indices from before and after reset are not mixed. The address filter includes writes that overlap the selected interval. The capture retains the newest 250,000 matching writes and reports how many older entries were discarded. The trace does not identify the SH-2 or DMA source of a write.

`-f` forces the emulator to start in fullscreen mode, ignoring the preference in Ymir.toml.

`-D` starts the emulator with debug tracing enabled.

For a frame-aligned VDP1 capture, enable **Debug > VDP > Command trace** and
then use **Debug > Dump all memory**. The dump now writes
`vdp1-command-trace.csv` and `vdp1-capture-bundle.txt` beside the VDP1/VDP2
VRAM and register files. Their frame cursor, command cursor, local coordinates,
trace row count and dropped-row count are sampled in the same emulation-thread
callback as the memory images. A nonzero `trace_dropped_total` means the
bounded trace buffer discarded earlier commands; `trace_file_complete=0`
means the CSV could not be fully written. This bundle provides frame
provenance for reverse engineering and does not assert game-specific meaning.

`-F` starts the emulator in fast-forward mode.

`-E` captures all unhandled exceptions, which can be useful to troubleshoot crashes or failure to start the emulator.

Note that the Windows version does not output anything to the console, but it does honor the command line parameters. You can pipe the output of the command to a file:

```sh
ymir-sdl3 > out.txt
```

Ymir requires an IPL (BIOS) ROM to work. You can place the ROMs under the `roms\ipl` directory created alongside the executable on the first run.
The emulator will scan and automatically select the IPL ROM matching the loaded disc. If no disc is loaded, it will use a ROM matching the first preferred region. Failing that, it will pick whatever is available.
You can override the selection on Settings > IPL.

Ymir can load game disc images from MAME CHD, BIN+CUE, IMG+CCD, MDF+MDS or ISO files. It does not support injecting .elf files directly at the moment.

When using low level CD block emulation (LLE), Ymir also requires the CD block ROM to be placed in `roms\cdb`.


## Troubleshooting

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) if the emulator crashes or misbehaves.

> [!NOTE]
> The author (@StrikerX3) works primarily with Windows and Linux systems and only provides support for other platforms on a best-effort basis.
> For Linux specifically, only the .tar.xz package on Ubuntu (both native and under WSL) is officially supported by the author.
> If you use custom-built Linux systems or anything that deviates too much from the norm, you're most likely on your own.
>
> The project relies on community support for other Linux packages, macOS and FreeBSD.


## Compiling

See [COMPILING.md](COMPILING.md).


## Support my work

If you enjoy my projects and want to help me keep developing them, consider supporting me:
- [Patreon](https://www.patreon.com/StrikerX3) for ongoing support
- PIX for one-time donations in Brazil: ask me on Discord.

Your support is completely optional but genuinely appreciated. It helps me dedicate more time and energy to these passion projects while keeping everything open-source and free for everyone. Thank you!
