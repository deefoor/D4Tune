# D4Tune

D4Tune is a relocatable WebAssembly runtime object for `nfsu2-web`. It adds **Tune Car** to career free roam's pause menu, opens the stock performance-tuning screens over the loaded world, and returns directly to free roam.

It does not build or replace another project's `game.wasm`. The target project links `d4tune.bin` into its own existing WebAssembly build.

This package contains no mod loader, cars, models, sounds, garage expansion, or save-format changes.

## Build D4Tune

Put this `D4Tune` folder directly in the root of the target `nfsu2-web` source checkout.

Linux or macOS:

```sh
./D4Tune/build.sh
```

Windows Command Prompt or PowerShell:

```bat
D4Tune\build.bat
```

This produces only:

```text
D4Tune/d4tune.bin
```

The build requires wasi-sdk 34 under the checkout's `deps/` directory, or `WASI` set to its directory. `build.bat` invokes `clang.exe` from the native Windows wasi-sdk directly; it does not require WSL or a Linux SDK.

## Integrate It

Apply the source integration patch:

```sh
git apply --3way D4Tune/integration.patch
```

The patch:

- Adds `runtime/pausemenu.c` as the shared owner of pause-menu `AddOption` and action dispatch.
- Refactors `runtime/photo.c` to use that shared dispatcher.
- Makes the existing Dyno handler forwardable by D4Tune.
- Registers the D4Tune install and frame callbacks.
- Makes `web/build.sh` link `D4Tune/d4tune.bin` as `d4tune.o`.
- Adds the default-off `d4tune_f5` option to the `settings.ini` template.

Then run the target project's normal build exactly as that project requires, for example:

```sh
web/build.sh
```

For a different object location, set `D4TUNE_OBJECT` for the target build:

```sh
D4TUNE_OBJECT="/path/to/d4tune.bin" web/build.sh
```

If the target has a customized runtime or build system and cannot apply the
patch, follow [`CUSTOM_INTEGRATION.md`](CUSTOM_INTEGRATION.md). It lists the
required dispatch ownership, callback order, source edits, link inputs, and a
short prompt suitable for an implementation agent.

The target project remains responsible for its own `game.wasm`, generated lift, loaders, cars, parts, sounds, saves, and deployment.

## Optional F5 Shortcut

The F5 shortcut is disabled by default. To open **Tune Car** with F5 while driving, add this line to `settings.ini` before starting the game:

```ini
d4tune_f5 = 1
```

The pause-menu entry remains available when the shortcut is disabled.

The patch was prepared against source revision `9e02302`. Modified forks should follow the custom integration guide and merge its behavior into their existing files rather than replacing those files.
