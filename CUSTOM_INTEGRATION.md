# D4Tune Custom Build Integration

Use this guide when your `nfsu2-web` source is customized enough that
`integration.patch` cannot be applied directly. It is written so a coding
agent can merge D4Tune into the custom tree without replacing unrelated work.

The patch and `src/runtime/d4tune.c` remain the source of truth. Read them
before adapting names, feature lists, or build scripts in a fork.

## What D4Tune Is

D4Tune opens the original NFSU2 performance-tuning screens over the world that
is already loaded. It does not provide a new UI and does not load another
world. The game still handles tuning categories, locks, sliders, car records,
profile saving, and the tuning-screen assets.

It adds **Tune Car** after **Garage** in career free roam's pause menu. The F5
shortcut is optional and disabled by default. Network, co-op, and split-screen
sessions are rejected.

`d4tune.bin` is a relocatable WebAssembly object. It is not a complete
`game.wasm`, a live-process plugin, or a file that can be loaded after the game
starts. It must be linked into the custom build's final `game.wasm`.

## Compatibility

D4Tune uses fixed addresses and layouts from `SPEED2.EXE` v1.2 NA. A custom
runtime can be integrated, but a different game executable requires a new
address and layout audit.

The supplied integration was prepared against `nfsu2-web` revision `9e02302`.
Never replace a newer fork's complete runtime files with files from that
revision.

## Files To Add Or Change

Add:

- `runtime/pausemenu.c`: the one shared owner of the pause-menu hooks. Copy its
  implementation from `integration.patch`, or merge the same callback chain
  into an existing shared pause-menu dispatcher.
- Either `D4Tune/d4tune.bin` as a linked object, or
  `runtime/d4tune.c` copied from `D4Tune/src/runtime/d4tune.c` and compiled by
  the custom build. Use exactly one, never both.

Modify, but do not replace:

- `runtime/dyno.c`: export its existing message handler and transfer Web
  ownership of the Dyno message dispatch to D4Tune.
- `runtime/photo.c`: make Photo Mode use the shared pause-menu dispatcher.
- `runtime/featurelist.h`: declare the shared pause-menu and D4Tune callbacks.
- `runtime/features.c`: install and run the new callbacks in the required
  order.
- The WebAssembly build script: compile `pausemenu.c` and link D4Tune's object.
- The settings parser/template: expose the default-off `d4tune_f5` option.
- The native/macOS build list, if the fork still builds it: compile
  `pausemenu.c`, but do not link the WebAssembly D4Tune object.

The reference patch changes comments in `runtime/coop.c` and
`runtime/savegame.c` only. They need no executable changes if their callback
signatures already match the shared dispatcher.

Do not replace or copy:

- The target's complete `photo.c`, `dyno.c`, `features.c`, or build scripts.
- Generated lift files or function tables from another checkout.
- Runtime headers from the reference checkout.
- A prebuilt final `game.wasm`.
- Loaders, cars, models, sounds, garage data, or save formats.

## 1. Add The Shared Pause-Menu Owner

Only one runtime feature may own each of these dispatches:

```text
0x0050DAB0  pause-menu AddOption
0x004C26B0  Replay-kind item action
sub_004c2640  stock Replay-item constructor override
```

Remove the old registrations for these addresses from `photo.c` or any other
feature. If the custom build already has a shared owner, merge the following
order into it instead of adding a second owner.

The AddOption chain must be:

```text
1. d4tune_before_add, if linked; stop when it returns nonzero
2. stock sub_0050dab0
3. savegame_item
4. d4tune_after_add, if linked
5. co-op item handling
6. Photo Mode item handling
```

The reference ordering uses an anchor rather than simply appending co-op first.
When Photo Mode is available, make its new item the co-op anchor and add the
Photo item by calling guest address `0x0050DAB0` again. That recursive call
runs the shared dispatcher and inserts co-op after Photo Mode. When Photo Mode
is unavailable, co-op anchors directly after Replay. Preserve this behavior or
the visible item order will change.

The Replay-action chain must be:

```text
1. d4tune_pause_accept
2. savegame_accept
3. coop_act
4. photo_pause_accept
5. stock sub_004c26b0
```

Preserve `coop_act`'s return contract from `integration.patch`: zero continues,
one makes the dispatcher perform the normal `0x14`-byte return, and two means
co-op already returned.

The shared owner must declare D4Tune's menu callbacks weak and check that each
one exists before calling it:

```c
__attribute__((weak)) int d4tune_before_add(CPU *, uint32_t, uint32_t);
__attribute__((weak)) void d4tune_after_add(CPU *, uint32_t, uint32_t);
__attribute__((weak)) int d4tune_pause_accept(CPU *);
```

Its `sub_004c2640(CPU *)` override must call `sub_004c2640_orig(CPU *)`
exactly once. If this override is new to the custom tree, regenerate the lift
using that project's normal override-discovery process.

The reference `pausemenu.c` directly depends on `savegame_item`,
`savegame_accept`, `coop_item`, `coop_act`, `photo_pause_replay`,
`photo_pause_item`, and `photo_pause_accept`. A custom build that omits Save,
co-op, or Photo Mode must make the absent feature's callbacks weak and guarded,
or provide no-op adapters. Do not copy the reference file unchanged and leave
unresolved symbols.

## 2. Refactor Photo Mode Without Replacing It

Keep all existing Photo Mode camera, rendering, input, and lifecycle code.
Only move its pause-menu ownership behind these callbacks:

```c
int photo_pause_replay(CPU *c, uint32_t item);
uint32_t photo_pause_item(CPU *c);
int photo_pause_accept(CPU *c);
```

`photo_pause_replay` decides whether Photo Mode follows the stock Replay item.
`photo_pause_item` creates and returns the item without adding it.
`photo_pause_accept` handles only the Photo Mode item and returns whether it
consumed the action.

Remove Photo Mode's old direct registrations for `0x0050DAB0` and
`0x004C26B0`, its copy of `sub_004c2640`, and any lazy frame-time installation
of those hooks. `runtime/pausemenu.c` must be their only owner.

## 3. Transfer Web Dyno Dispatch Ownership

Rename or expose the existing private Dyno message-handler body as:

```c
void dyno_dispatch(CPU *c);
```

It must continue to forward ordinary messages to the stock
`sub_005647e0(c)` implementation.

On WebAssembly, D4Tune must be the only owner of dispatch address
`0x005647E0`. Keep native ownership in `dyno_install`:

```c
void dyno_install(void) {
#ifndef __wasm__
    dispatch_patch(0x005647e0u, dyno_dispatch);
#endif
}
```

D4Tune processes its own exit and save messages, then calls `dyno_dispatch`
for ordinary Dyno behavior. Registering both handlers at the same address will
silently disable whichever one was installed first.

D4Tune also owns `0x0055A740` while linked and forwards unhandled messages to
the generated stock `sub_0055a740` function. It must be the only dispatch owner
for that address in the Web build.

## 4. Register The Features

Add these declarations to the target's feature declarations:

```c
void pausemenu_install(void);
void d4tune_install(void) __attribute__((weak));
void d4tune_frame(CPU *c) __attribute__((weak));
```

For a D4Tune-enabled Web build, the install order must be:

```text
dyno_install
pausemenu_install
d4tune_install
regional_install
```

Register `d4tune_install` for Web builds only. The D4Tune object is mandatory
in that build, so its install callback may be called directly.

The frame order around these features must be:

```text
photo_frame
d4tune_frame
savegame_frame
coop_frame
```

The reference integration uses a small guard because the declaration is weak:

```c
static void f_d4tune(CPU *c) {
    if (d4tune_frame) d4tune_frame(c);
}
```

## 5. Link D4Tune Into `game.wasm`

The safest option for a heavily customized runtime is to compile
`D4Tune/src/runtime/d4tune.c` against that runtime's actual headers. The
simplest option for a compatible runtime is to link the supplied object.

To use the supplied object, copy it into the target's runtime object directory
as `d4tune.o` and include it in the final link:

```sh
d4="${D4TUNE_OBJECT:-$ROOT/D4Tune/d4tune.bin}"
[ -f "$d4" ] || { echo "D4Tune object not found: $d4"; exit 1; }
cp "$d4" "$OUT/rt/d4tune.o"
```

Also compile `runtime/pausemenu.c`. A custom linker that lists objects
explicitly must include both `pausemenu.o` and `d4tune.o`.

To compile the source instead, use the same Web runtime ABI and flags as the
target's other runtime C files. The package builders show the required
`wasm32-wasip1-threads`, WASI compatibility, feature, include, and
`-fno-delete-null-pointer-checks` flags.

D4Tune expects these target services and symbols to exist:

```text
autotest_frame       career_fe_active      coop_in
coop_racing_together dikbd_ours            dispatch_patch
dispatch_indirect    dyno_dispatch         garage_find
gheap_alloc
kbd_ev_get           kbd_ev_head           language_add_text
sub_0055a740          fprintf/stderr        getenv
strcasecmp           g_cbits               the normal dispatch/table runtime
```

An unresolved symbol or WebAssembly signature mismatch means the custom
runtime API differs and must be adapted at the source level. Do not solve it by
copying old runtime headers into the custom tree.

## 6. Add The Setting

Map this `settings.ini` key to the environment before runtime installation:

```ini
# Open Tune Car with F5 while driving. The career free-roam pause menu has
# Tune Car either way. [NFSU2_D4TUNE_F5]
#d4tune_f5 = off
```

The equivalent reference settings-table entry is:

```ts
{ key: 'd4tune_f5', env: 'NFSU2_D4TUNE_F5', kind: 'bool', scope: 'web' }
```

D4Tune reads `NFSU2_D4TUNE_F5` once during `d4tune_install`. Its default is
off. The pause-menu item is always available in valid career free-roam states;
this option controls only F5.

## How It Works At Runtime

1. The shared pause dispatcher inserts **Tune Car** after **Garage** and before
   **Options**.
2. D4Tune resolves the driven car's owned front-end record and initializes the
   game's temporary Dyno tuning context without starting a world load.
3. It loads `Global\InGamePerformanceTuning.bun` with the stock bundle loader.
4. It atomically replaces `UI_Pause.fng` with the stock tuning mode picker.
5. The stock screen factories, category locks, installed-part checks, sliders,
   car records, and profile-save jobs do the actual tuning work.
6. D4Tune removes Dyno Run, Test Run, and Quit Tuning from its active menu.
   Save Profile remains.
7. Back checks whether the setup changed. If it did, Yes or No applies the
   setup after the player answers; Yes also starts the stock profile-save job.
8. D4Tune closes the temporary context, removes its UI packages, and resumes
   the already loaded world.

The ordinary profile-save system remains responsible for persistence. D4Tune
does not change the save format.

## Verification Checklist

- The Web build contains exactly one D4Tune implementation: object or source.
- One shared owner handles `0x0050DAB0`, `0x004C26B0`, and `sub_004c2640`.
- Web has only D4Tune owning `0x005647E0`; native has only `dyno_install`.
- The final link resolves every D4Tune import with no signature warnings.
- Replay, Save, Photo Mode, and local co-op pause items still work.
- Ordinary Dyno visits and saving still work.
- **Tune Car** appears after **Garage** only in career free roam.
- Opening and closing it repeatedly does not stack menus or reload the world.
- Dyno Run, Test Run, and Quit Tuning disappear only while D4Tune is active.
- Back unchanged, Back/Yes, Back/No, and dialog Cancel all return safely.
- A saved tune persists after a normal profile reload.
- F5 does nothing by default and works after `d4tune_f5 = 1` and a new launch.
- Network, co-op, split-screen, overlays, and invalid car states cannot open it.
- Native/macOS still links with its original Dyno behavior, if supported.

## Short Agent Prompt

Give an implementation agent this directory and the following instruction:

```text
Integrate D4Tune into this custom nfsu2-web game.wasm build without replacing
existing runtime files. Read D4Tune/CUSTOM_INTEGRATION.md,
D4Tune/integration.patch, and D4Tune/src/runtime/d4tune.c first. Merge the
shared pause-menu dispatch, Photo Mode callbacks, Web Dyno dispatch ownership,
feature registration, object/source link, and default-off d4tune_f5 setting
into this tree's existing architecture. Preserve all fork-specific features.
Use exactly one D4Tune implementation, regenerate the lift only if the moved
sub_004c2640 override is new, build the final game.wasm, and run the checklist
from CUSTOM_INTEGRATION.md. Report every adaptation made for this fork.
```
