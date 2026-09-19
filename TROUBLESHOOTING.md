# Troubleshooting

First and most important: make sure you're using the [latest version](https://github.com/ymir-emu/Ymir/releases/latest) or give the [nightly build](https://github.com/ymir-emu/Ymir/releases/latest-nightly) a try. There's a good chance your issue has already been solved in a recent update.

Also check for [open issues](https://github.com/ymir-emu/Ymir/issues) on the GitHub repository. Use the search bar! Remove the `state:open` to also list closed issues -- those may include tips and hints that could help fix your problem.

Take a look at the [official compatibility list](https://docs.google.com/spreadsheets/d/1SLZzL9LelSlpEmTKy8cjaQnE7mew2uW1rfCgcekO58Q/edit?usp=sharing) to see if there are any notes about the game. Some games require specific settings to work or may have worked in older versions of the emulator.

If you're on Windows, install the latest [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vc14/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vc14/vc_redist.arm64.exe)).
This is mandatory to make Ymir work.

If those didn't help, follow the instructions below.


## Ymir fails to launch, crashes right away or displays a big "fatal error" popup

Windows users: install the [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vc14/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vc14/vc_redist.arm64.exe)) before launching Ymir. Installing other software might replace important system files with older versions that are incompatible with the emulator. As stated in the [README](README.md), this is **mandatory** to prevent early crashes.

If the emulator is crashing early:
- Make sure to download a version compatible with your CPU. The AVX2 version requires newer, usually more powerful CPUs, so if you have a Core i3 or i5 from older generations (3xxx or less), a Pentium or a Celeron, the first thing to try is to test the SSE2 version instead.
- Ymir specifically requires the default audio output device to be present upon startup. If you set your headphones as the default device, ensure they're plugged in before launching the emulator.

If you get the "fatal error" popup, you encountered a critical bug in the application. The best course of action is to collect a memory dump which can pinpoint the exact piece of code that caused the problem.

> [!IMPORTANT]
> To collect a memory dump, you **must use a nightly build** as they include debug symbols needed to make these dumps useful.
>
> While sharing the dump, **mention the full Ymir version string** (copied from the Help > About window) and **whether you're using the SSE2, AVX2 or ARM64/NEON version**.
>
> When you encounter a fatal error, **leave the popup open** as you follow the instructions for your operating system below.

### Windows

Follow these steps to collect and share a minidump:
1. Download ProcDump: https://learn.microsoft.com/en-us/sysinternals/downloads/procdump.
2. Open a Command Prompt window (cmd.exe), navigate to the folder where you downloaded ProcDump (`cd <path-to-folder>`) and run `procdump ymir-sdl3.exe`.
3. Open the folder from which you ran the command (you can run `start .` from the Command Prompt to open an Explorer window on that directory). There should be a file named `ymir-sdl3.exe_<date>_<time>.dmp`. Compress that and share it. This file contains a minimal dump of the program which can be used by developers to figure out where exactly the emulator crashed.
   - For developers: the PDBs can be found attached to the [nightly release workflow](https://github.com/ymir-emu/Ymir/actions/workflows/nightly-release.yaml).

### Linux, macOS, FreeBSD

1. Enable core dumps temporarily (if you haven't already enabled them system-wide):
    ```sh
    ulimit -c unlimited
    ```
2. Run the emulator from the same shell session.
3. When the crash occurs, open a new shell and collect the dump:
    1. Find the PID of the process:

        ```sh
        pgrep ymir-sdl3
        ```
        or:
        ```sh
        ps a | grep ymir-sdl3
        ```
    2. Generate the core dump:

        Linux:
        ```sh
        gcore -o ymir.dmp <pid>
        ```
        FreeBSD:
        ```sh
        gcore -c ymir.dmp <pid>
        ```
        or:
        ```sh
        kill -6 <PID>
        ```
        NOTE: `kill -6` sends a `SIGABRT` signal to the process, causing the core dump to be saved to the default core dumps location in your system.
    3. Compress and upload the dump file.


## "No IPL ROM found" message when loading any game

IPL ROM is the Saturn BIOS ROM, which is required for Ymir to work. You need to place it in the `<profile>/roms/ipl` directory. The emulator will automatically detect and load the file as soon as you place it in the directory.

Most people that ask about this have skipped the Welcome dialog explaining this step. The Welcome screen also includes a clickable link for the path and will go away on its own as soon as a valid IPL ROM is placed in the directory. Remember: **read *everything***!


## My controller doesn't work or some buttons don't respond

Ymir currently works best with XInput controllers, that is, anything that behaves like an Xbox controller. Third-party controllers like 8bitdo sometimes offer a toggle or a way to enable XInput mode on their controllers which usually improves compatibility.

Ymir also uses SDL's game controller database, with data pulled from https://github.com/mdqinc/SDL_GameControllerDB on every build. You can manually replace the gamecontrollerdb.txt file with an updated one from this repository if your controller is not yet recognized.

Related issues: [#482](https://github.com/ymir-emu/Ymir/issues/482)

There are plans to improve compatibility with other controllers in the future, but it's not high in the priority list.


## Game discs don't load with the Flatpak release

Flatpak uses sandboxing and restricts access to the file system by default. Ymir's package doesn't grant any filesystem permissions beyond the defaults, so you'll need to manually grant access:
1. In **Flatseal**, find *Ymir*.
2. In the **Filesystem** section, do either of these:
   - **(Recommended)** Add a new entry in **Other files** and type the directory where you store your disc images (e.g. `~/Roms/Saturn`). Consider using read-only mode (suffix the path with `:ro`, e.g. `~/Roms/Saturn:ro`) as Ymir doesn't write to disc images.
   - **(Less secure)** Enable **All user files** if your ROMs live in your home directory.

If your files are stored on an external storage device such as an SD card, you will need to write the full path. For example, `/run/media/SDCARDNAME/ROMs/Saturn`. You can locate the external storage path by navigating to the device in the file manager and clicking the path in the address bar.


## Ymir runs too slowly

Here are a few things you can try to improve performance besides upgrading the CPU, roughly in order of performance impact:
- Stop any background programs you're running, including things like Rainmeter. Some RGB tools are also known for being CPU hogs.
- In the **Debug** menu, make sure **Enable tracing** is disabled. (Shortcut is F11 by default.)
- In **Settings > General**, check that the **Emulation speed** is set to **Primary** and it is at **100%**. Press **Reset** to restore the default speed.
- In **Settings > CD Block**, disable **Use low level CD Block emulation**. Most games work fine without it.
- In **Settings > System**:
  - Lower the **SH-2 clock ratio** *carefully*. Most games take the slower CPU gracefully, but a few might break.
    - If games break due to this setting, reset it to 100%. Issue reports for problems caused by tweaking this will be rejected.
  - Disable **Emulate SH-2 cache** if possible. Most games work fine without it.
    - This option is force-enabled with a few select games.
- In **Settings > Audio**, set **Emulation step granularity** to the minimum possible value of **0**, all the way to the left. It should read **Step size: 32 slots (1 sample)**.
- In **Settings > Video**:
  - Disable **Use full refresh rate when synchronizing video**. This is known to cause problems in cases where the reported refresh rate does not match the actual display refresh rate.
  - Disable **Synchronize video in windowed mode** and/or **Synchronize video in full screen mode**. These also tend to cause performance issues with mismatched refresh rate reports.
  - Enable **Threaded VDP2 renderer**.
  - Enable **Use dedicated thread for deinterlaced rendering**.
  - Try enabling or disabling **Threaded VDP1 renderer**.
  - Disable the **Deinterlace video** enhancement.
  - Disable the **Transparent meshes** enhancement.
- In **Settings > General**:
  - Disable the **Rewind Buffer**. (Shortcut is F8 by default.)
    - You can also find this option in the **Emulation** menu.
    - If you wish to use the Rewind Buffer, try lowering the **Compression level** in **Settings > General**. This will increase memory usage.
  - Enable **Boost process priority**.
  - Enable **Boost emulator thread priority**.
- If you're experiencing stutters during gameplay:
  - Use an uncompressed disc image (anything other than CHD).
  - Load disc images from a fast local disk, preferably an SSD.
  - Go to **Settings > General** and enable **Preload disc images to RAM**. This will increase memory usage and hang the emulator for a while when loading discs, but should eliminate all stutters.
- Use the AVX2 version if you can. If the emulator crashes right away, it's likely that your CPU doesn't support the instruction set, so you're stuck with the SSE2 version.

If Ymir still runs poorly after trying these, your CPU might be too slow for the emulator. It's known to run fine on CPUs that score around 1500 points on the [CPUBenchmark single thread test](<https://www.cpubenchmark.net/single-thread>), but I recommend CPUs that score 2000 points or higher.
A quad core CPU or better will help with threaded VDP1/VDP2 rendering, threaded deinterlace and the rewind buffer.


## Errors when selecting Direct3D 11/12, Vulkan or Metal

Make sure your GPU meets the minimum requirements for Ymir:
- Direct3D 11: TBD
- Direct3D 12: Feature level 11.0 and Shader Model 6.0
- Vulkan: 1.1
- Metal: Any Metal-capable Mac (GPU family Common1)

Update your GPU drivers:
- Nvidia: [Find your GPU here](https://www.nvidia.com/Download/index.aspx?lang=en-us) or [use the Nvidia App to update drivers automatically](https://www.nvidia.com/en-us/software/nvidia-app/)
- AMD: [Find your GPU or CPU here](https://www.amd.com/en/support) or use the download buttons at the top to update drivers automatically
- Intel: [Find your GPU or CPU here](https://www.intel.com/content/www/us/en/download-center/home.html)

> [!IMPORTANT]
> Do not use Windows Device Manager or Windows Update to update your GPU drivers, as they tend to have poor performance and lack support for advanced GPU features.
>
> Always update your integrated GPU driver first, even if you're not using it. This also applies to desktop PCs with dedicated graphics cards.

If that doesn't help, open a new issue and include your exact GPU model (and CPU model if it's an integrated GPU) and the graphics backend you're trying to use.
On Windows, dxdiag information is very useful:
- Run `dxdiag`
- Click **Save All Information...** and save DxDiag.txt
- Include DxDiag.txt in the issue


## General issues on Windows

- Software like [Rainmeter](https://www.rainmeter.net/) and some RGB controllers can be very demanding on low-end systems. Make sure to turn them off when not needed for optimal performance.
- Desktop customization software such as [Windhawk](https://windhawk.net/) and [OldNewExplorer](https://www.oldnewexplorer.com/) can interfere with some basic application functions and cause Ymir to freeze or crash. Disable them or add Ymir to exceptions if possible.


## General issues on Linux

For typical Linux systems:
- The system needs to provide an XDG-compliant desktop environment for file and folder selection dialogs. Without it, your only option to load discs is by dragging files onto the window or launching the emulator with a disc path via the command line.
- Ymir needs access to the filesystem to load game discs. The Flatpak release in particular is prone to causing problems due to its sandboxing rules.
- The ALSA driver (if present) must be set up correctly for the emulator to launch. Some systems or setups have broken ALSA drivers or incorrect file access permissions causing the application to crash.
- Some SDL3 crashes can be worked around by launching the application with the `SDL_AUDIODRIVER` environment variable set to `alsa`.

If you're using a Linux distribution that deviates too much from a typical Debian/Fedora/Arch setup, you'll likely encounter various issues launching or using the emulator.
- If you're on NixOS, try using `steam-run` to launch the emulator.
- If you encounter any issues not listed here, you're on your own. Feel free to open a PR to include new troubleshooting instructions for fellow Linux users here!
