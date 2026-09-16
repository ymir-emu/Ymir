# Compiling Ymir

Ymir requires [CMake 3.28+](https://cmake.org/) and a C++20 compiler.

The repository includes several vendored dependencies as Git submodules. When cloning, make sure to include the `--recurse-submodules` parameter.
You can also initialize submodules with `git submodule update --init --recursive` after a `git clone` or when pulling changes.

Ymir has been successfully compiled with the following toolchains:
- Visual Studio 2022's Clang 19.1.5
- Visual Studio 2022's MSVC 19.44.35213.0
- Visual Studio 2026's Clang 20.1.8
- Visual Studio 2026's MSVC 19.50.35724.0
- Clang 15.0.7 on WSL Ubuntu 24.04.5 LTS (`clang-15` / `clang++-15`)
- Clang 18.1.3 on WSL Ubuntu 24.04.5 LTS (`clang` / `clang++`)
- Clang 19.1.1 on Ubuntu 24.04.2 LTS (`clang-19` / `clang++-19`)
- GCC 14.2.0 on Ubuntu 24.04.2 LTS (`gcc-14` / `g++-14`)
- Clang 19.1.7 on FreeBSD 14.3-RELEASE (`clang19` / `clang++19`)
- Clang 21.1.0 on FreeBSD 14.3-RELEASE (`clang21` / `clang++21`)
- Apple Clang 17 on macOS 15 Sequoia

The project has been compiled for x86_64 and ARM64 Windows, Linux, FreeBSD and macOS platforms.

Clang is the preferred compiler for its multiplatform support and excellent code generation. Ymir requires Clang 15 or later.
Ninja is the preferred build system for its speed and feature support. Makefiles are also acceptable.


## Build configuration

You can tune the build with following CMake options:

- `Ymir_AVX2` (`BOOL`): Set to `ON` to use AVX2 extensions (on x86_64 platforms only). `OFF` uses the platform's default instruction set, typically SSE2. ARM64 platforms will always use NEON. Disabled by default.
- `Ymir_ENABLE_TESTS` (`BOOL`): Includes the unit test project in the build. Enabled by default if this is the top level CMake project.
- `Ymir_ENABLE_SANDBOX` (`BOOL`): Includes the sandbox project in the build. Enabled by default if this is the top level CMake project.
- `Ymir_ENABLE_YMDASM` (`BOOL`): Includes the disassembly tool project in the build. Enabled by default if this is the top level CMake project.
- `Ymir_ENABLE_IPO` (`BOOL`): Enables interprocedural optimizations (also called link-time optimizations) on all projects. Enabled by default.
- `Ymir_ENABLE_DEVLOG` (`BOOL`): Enables logs meant to aid development. Enabled by default.
- `Ymir_ENABLE_DEV_ASSERTIONS` (`BOOL`): Enables development assertions, meant to mark code as incomplete or for potential bugs. Disabled by default.
- `Ymir_ENABLE_IMGUI_DEMO` (`BOOL`): Enables the ImGui demo window, useful as a reference when developing new UI elements. Enabled by default.
- `Ymir_ENABLE_UPDATE_CHECKS` (`BOOL`): Enables automatic update checks and onboarding process. Enabled by default.
- `Ymir_EXTRA_INLINING` (`BOOL`): Enables more aggressive inlining, which slows down the build in exchange for better runtime performance. Only applies to Clang, which handles heavy inlining much better than GCC or MSVC. Disabled by default.
- `Ymir_PGO` (`STRING`): PGO mode. Valid values are `OFF`, `GENERATE`, `USE`. Defaults to `OFF`.
- `Ymir_PGO_DIR` (`PATH`): Directory where PGO profile data is written. Defaults to `${CMAKE_BINARY_DIR}/pgo-profdata`.
- `Ymir_PGO_PROFDATA` (`FILEPATH`): Merged LLVM PGO profile data path. Defaults to `${Ymir_PGO_DIR}/ymir.profdata`.
- `Ymir_LIBRARY_ONLY` (`BOOL`): Compiles `ymir-core` only, for use as a subproject in another CMake project. Defaults to `ON` if included as subproject, `OFF` otherwise.

These options are used by the build workflows to tune the build output:

- `Ymir_DEV_BUILD` (`BOOL`): Create a development build. This affects the versioning scheme (`-dev` suffix added, "(development build)" added to About window) and availability of development-friendly features (e.g. disabled automatic update checks).
  Enabled by default, and should probably not be disabled unless you're checking behavior of stable and nightly builds.

Ymir also supports feature flags. These are enabled by default on development and nightly builds:

- `Ymir_FEATUREFLAG_DEFAULT` (`BOOL`): Enables or disables all non-overridden feature flags. Enabled by default on development builds.
- `Ymir_FF_HOST_CD_DRIVES` (`BOOL`): Enables support for reading discs from host CD drives (physical or virtual). Disabled by default, including development and nightly builds.
<!-- Template:
- `Ymir_FF_[NAME]` (`BOOL`): Enables [feature].
-->

Feature flags are made available to code as macros in [ymir-core's CMakeLists.txt](libs/ymir-core/CMakeLists.txt) (find `## Define feature flags macros`).
If you add new feature flags, make sure to add the macro to this file too.

Depending on the target build system, you might have to specify `CMAKE_BUILD_TYPE` for Release builds, otherwise CMake defaults to slow Debug builds.
This option can be set to one of these values (see [docs](https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html)):

- `Debug`: an unoptimized Debug build, with debug assertions and no inlining. Ideal for deep code debugging, but very slow.
- `Release`: optimized Release build without debug symbols. Best used for stable and nightly builds. Probably not useful for development.
- `RelWithDebInfo`: same as `Release`, but includes debug symbols. Great for general development, but the optimizations and inlining might get in the way of debugging.
- `MinSizeRel`: a Release build that is optimized for code size. Not very useful since we're not targeting memory-constrained systems.

For a Release build, you might want to disable the devlog and ImGui demo window and enable extra inlining to maximize performance and reduce the binary size.

It is highly recommended to use [Ninja](https://ninja-build.org/) as it greatly accelerates the build process, especially on machines with high CPU core counts.


## Building on Windows

To build Ymir on Windows, you will need [Visual Studio 2022 Community](https://visualstudio.microsoft.com/vs/community/) or later and [CMake 3.28+](https://cmake.org/).
Clang is highly recommended over MSVC as it produces much higher quality code, outperforming MSVC by 50-80%. However, MSVC tends to provide a better debugging experience.

You may also want to install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home). You can do so by downloading and running the installer or with the following command:

```sh
winget install KhronosGroup.VulkanSDK
```

Vulkan is an optional dependency on Windows as Ymir always supports Direct3D 12 and 11 on this operating system.

All other dependencies are included through `vcpkg` and in the `vendor` directory, and are built together with the emulator.
The core library does not consume any vcpkg dependencies.

You can choose to generate a .sln file with CMake or open the directory directly with Visual Studio.
Both methods work, but opening the directory allows Visual Studio to use Ninja for significantly faster build times.
If you choose to generate the .sln file, you will need to specify the vcpkg toolchain:

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
```

Ymir uses custom Windows triplets to ensure all libraries are statically linked when possible. You can find the overridden triplets in the `vcpkg-triplets` folder.


## Building on Linux

To build Ymir on Linux, first you will need to install SDL3's required dependencies. Follow the instructions on [this page](https://wiki.libsdl.org/SDL3/README-linux) to install them.
You might also have to install additional packages:
- `autoconf autoconf-archive automake libtool` for ALSA
- `python3 python3-venv` for dbus

Vulkan is an optional dependency which enables GPU-accelerated VDP1/VDP2 rendering. For that reason, it is highly recommended to install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home).
If Vulkan is enabled, you will also need `dxc` to allow Ymir to compile shaders offline. DXC is usually included with the SDK. shaderc (`glslc`) is not supported due to usage of modern HLSL features in some shaders (e.g. 64-bit integers).
You can opt to install the Vulkan dependencies from your system's package manager instead of the SDK. For example, on Ubuntu: `libvulkan-dev vulkan-tools vulkan-validationlayers spirv-tools-dev glslc glslang-tools`.

The compiler of choice for this platform is Clang. GCC is also supported, but produces slightly slower code.

Use CMake to generate a Makefile or (preferably) a Ninja build script:

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
```

Pass additional `-D<option>=<value>` parameters to tune the build. See the [Build configuration](#build-configuration) section above for details.

`CMAKE_BUILD_TYPE` defaults to `Debug`. Use `-DCMAKE_BUILD_TYPE=RelWithDebInfo` for a Release build with debug symbols.

You can use CMake to build the project, regardless of generator:

```sh
cmake --build build --parallel
```


## Building on FreeBSD

Install required packages:

```sh
pkg install cmake evdev-proto git gmake libX11 libXcursor libXext libXfixes libXi \
    libXrandr libXrender libXScrnSaver libXtst libglvnd libinotify llvm19 ninja patchelf \
    pkgconf python3 vulkan-loader zip glslang shaderc vulkan-headers dxc
```

Notes:
- A default FreeBSD installation provides a stripped-down LLVM toolchain which lacks
  the required `clang-scan-deps` binary. Therefore it is necessary to install a
  complete LLVM toolchain package, e.g. `llvm19`.
- The usage of CMake's "Precompile Headers" feature triggers a compiler bug in LLVM
  prior to version 21 for ARM64 builds on FreeBSD. Therefore it is necessary to install
  and use at least `llvm21` for ARM64.

Configure build:

```sh
CXX=clang++19 \
CC=clang19 \
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
```

Notes:
- By default vcpkg and CMake will use the stripped-down LLVM toolchain instead of
  the previously installed complete toolchain. Therefore it is necessary to set
  the `CXX` and `CC` environment variables to the correct compilers.

Pass additional `-D<option>=<value>` parameters to tune the build. See the [Build configuration](#build-configuration) section above for details.

Build:

```sh
cmake --build build --parallel
```

Ymir uses SDL3's Dialog API. This requires an installed dialog driver in order for
the file dialogs to work in Ymir. Install Zenity:

```sh
pkg install zenity
```


## Building on macOS

Use CMake to generate a Makefile or (preferably) a Ninja build script:

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
```

Pass additional `-D<option>=<value>` parameters to tune the build. See the [Build configuration](#build-configuration) section above for details.

`CMAKE_BUILD_TYPE` defaults to `Debug`. Use `-DCMAKE_BUILD_TYPE=RelWithDebInfo` for a Release build with debug symbols.

You can use CMake to build the project, regardless of generator:

```sh
cmake --build build --parallel
```


After building, you will find the .app bundle at:
```sh
build/apps/ymir-sdl3/ymir-sdl3.app
```


## Profile Guided Optimization (PGO)

PGO is supported for Clang/AppleClang and GCC (MSVC PGO may be added in future releases).
Use a two-phase build: first generate profile data, then rebuild using that data.


### Windows - Visual Studio IDE, open folder

With the project opened as a folder:
1. Select the **Full x86-64 [arch] Clang Release/Dist (PGO Generate)** target.
2. Build and run a representative workload. This will produce a `.profraw` file in `out\pgo-profdata\windows\x86_64-[arch]`.
3. In the **Solution Explorer** view, switch to the **CMake Targets** view. You can also find it in the **View** menu.
4. Build the **`ymir-pgo-merge`** utility target. This will combine the `.profraw` files into `ymir.profdata`.
5. Select the **Full x86-64 [arch] Clang Release/Dist (PGO Use)** target.
6. Build and run. This build will use profile-guided optimization with the profiling data you acquired from the previous run.


### Windows - Visual Studio command line tools

1. Launch the x64 Native Tools Command Prompt for VS 2022/2026.
2. Generate an instrumented build:

    ```cmd
    cmake -S . -B build-pgo-gen -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake ^
      -DYmir_PGO=GENERATE
    cmake --build build-pgo-gen --parallel
    ```

3. Run a representative workload and emit `.profraw` into the PGO directory:

    ```cmd
    set LLVM_PROFILE_FILE="%CD%\build-pgo-gen\pgo-profdata\ymir_%p.profraw"
    .\build-pgo-gen\apps\ymir-sdl3\ymir-sdl3
    ```

4. Merge the raw profiles using the CMake target:

    ```cmd
    cmake --build build-pgo-gen --target ymir-pgo-merge
    ```

    Or manually with `llvm-profdata`:

    ```cmd
    llvm-profdata merge ^
      -o "%CD%\build-pgo-gen\pgo-profdata\ymir.profdata" ^
      "%CD%\build-pgo-gen\pgo-profdata"\*.profraw
    ```

5. Build using the merged profile:

    ```cmd
    cmake -S . -B build-pgo-use -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DYmir_PGO=USE ^
      -DYmir_PGO_PROFDATA="%CD%\build-pgo-gen\pgo-profdata\ymir.profdata"
    cmake --build build-pgo-use --parallel
    ```



### Linux and macOS - Clang / AppleClang

1. Generate an instrumented build:

    **Linux**
    ```sh
    cmake -S . -B build-pgo-gen -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DYmir_PGO=GENERATE
    cmake --build build-pgo-gen --parallel
    ```

    **macOS**
    ```sh
    cmake -S . -B build-pgo-gen -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
      -DYmir_PGO=GENERATE
    cmake --build build-pgo-gen --parallel
    ```

2. Run a representative workload and emit `.profraw` into the PGO directory:

    ```sh
    export LLVM_PROFILE_FILE="$PWD/build-pgo-gen/pgo-profdata/ymir_%p.profraw"
    ./build-pgo-gen/apps/ymir-sdl3/ymir-sdl3
    ```

3. Merge the raw profiles using the CMake target:

    ```sh
    cmake --build build-pgo-gen --target ymir-pgo-merge
    ```

    Or manually with `llvm-profdata`:

    ```sh
    llvm-profdata merge \
      -o "$PWD/build-pgo-gen/pgo-profdata/ymir.profdata" \
      "$PWD/build-pgo-gen/pgo-profdata"/*.profraw
    ```

4. Build using the merged profile:

    **Linux**
    ```sh
    cmake -S . -B build-pgo-use -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DYmir_PGO=USE \
      -DYmir_PGO_PROFDATA="$PWD/build-pgo-gen/pgo-profdata/ymir.profdata"
    cmake --build build-pgo-use --parallel
    ```

    **macOS**
    ```sh
    cmake -S . -B build-pgo-use -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
      -DYmir_PGO=USE \
      -DYmir_PGO_PROFDATA="$PWD/build-pgo-gen/pgo-profdata/ymir.profdata"
    cmake --build build-pgo-use --parallel
    ```

### GCC

1. Generate an instrumented build:

    ```sh
    cmake -S . -B build-pgo-gen -DCMAKE_BUILD_TYPE=Release -DYmir_PGO=GENERATE
    cmake --build build-pgo-gen --parallel
    ```

2. Run a representative workload to emit `.gcda` data into `build-pgo-gen/pgo-profdata/gcc`:

    ```sh
    ./build-pgo-gen/apps/ymir-sdl3/ymir-sdl3
    ```

    Profiles will appear in the subdirectory tree, not flat.

3. Build using the generated profiles (point the use build at the generate build directory):

    ```sh
    cmake -S . -B build-pgo-use -DCMAKE_BUILD_TYPE=Release \
      -DYmir_PGO=USE \
      -DYmir_PGO_DIR="$PWD/build-pgo-gen/pgo-profdata"
    cmake --build build-pgo-use --parallel
    ```



# Consuming Ymir as a library

When Ymir is included as a subproject inside another CMake project, it automatically sets `Ymir_LIBRARY_ONLY=ON`, which
reconfigures the project to only build the `ymir::ymir-core` static library target without using vcpkg.

The easiest ways to use Ymir as a library are to include it as a Git submodule or by using CMake's `FetchContent`.
Whatever method is used, be patient -- Ymir includes several large Git submodules that can take some time to download.

> [!IMPORTANT]
> Before v0.3.2, Ymir did not offer the option to build the core library alone and required vcpkg to work. You will not
> be able to use older versions of the emulator as a library in this manner without a ton of headaches due to the
> combined use of vcpkg integration *and* Git submodules as dependencies.
> 
> Version 0.3.2 fixes this by offering the `Ymir_LIBRARY_ONLY` option explained above.


## Using a Git submodule

It is recommended to keep your vendored dependencies in a subdirectory of your repository, usually called `vendor` or
`third_party`. Inside it, run these commands:

```sh
git submodule add https://github.com/ymir-emu/Ymir.git
git submodule update --init --recursive
```

Once that is done, adding Ymir to your project is as simple as adding these instructions to your CMakeLists.txt:

```cmake
add_subdirectory(vendor/Ymir)

target_link_libraries(your-target PRIVATE ymir-core)
```

With this method, you can easily keep your dependency up-to-date by `git pull`ing the latest commit from within the
`Ymir` subdirectory. This method also lets you tinker with the emulator's source code at any time.


## Using FetchContent

With [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html), the Git cloning process is automated
by CMake. Add these instructions to your CMakeLists.txt:

```cmake
include(FetchContent)

FetchContent_Declare(
    ymir
    GIT_REPOSITORY https://github.com/ymir-emu/Ymir
    GIT_TAG        v0.3.2   # ideally, a specific tag or commit, but `main` also works
)

FetchContent_MakeAvailable(ymir)
```

To link your target against Ymir:

```cmake
add_subdirectory(vendor/Ymir)

target_link_libraries(your-target PRIVATE ymir-core)
```

With this option, you cannot modify Ymir's source code. Additionally, the dependency is recursively cloned for each
build configuration you use, which could waste disk space.
