# Vexland

> A Hyprland-inspired dynamic tiling window manager for X11.

Vexland is an experimental X11 window manager written in C++23.

The project aims to bring the window management experience, dynamic tiling
and visual behavior inspired by Hyprland to traditional X11/Xorg desktops.

Vexland is **not a Wayland compositor**. It is an X11 window manager, lol hi.

## Features

- Dynamic tiling window management
- Dwindle-style layout
- Workspaces
- Window focus management
- XRandR monitor detection
- XCB-based X11 window management
- XKB / xkbcommon-based keybind handling
- Lua configuration
- Configurable gaps
- X11 window opacity support
- picom integration for visual effects
- Multi-monitor support

## Requirements

- CMake >= 3.20
- C++23 compiler
- pkg-config
- XCB
- XCB RandR
- XCB XKB
- xkbcommon
- xkbcommon-x11
- Lua 5.4

## Building

Clone the repository:

```bash
git clone https://github.com/MaybeWeDead/vexland.git
cd vexland
```

Configure and build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The resulting binary will be:

```text
build/vexland
```

## Running

Vexland is an X11 window manager, so it requires an Xorg/X11 session.

You can test it from an existing X11 session with:

```bash
./build/vexland
```

For testing, it is recommended to use a nested X server or a separate X11
session rather than replacing your desktop window manager immediately.

> Vexland is experimental software. Keep a way to recover your graphical
> session before testing it as your primary window manager.

## Configuration

Vexland uses Lua for configuration.

The configuration system provides functionality such as:

```lua
vx.set(...)
vx.bind(...)
```

Configuration support is still under development, so available options may
change between releases.

## Architecture

The project is split into several main subsystems:

```text
src/
├── desktop/
│   ├── view/
│   │   ├── Window
│   │   └── WindowState
│   ├── state/
│   │   └── FocusState
│   └── Workspace
│
├── output/
│   └── MonitorState
│
├── layout/
│   ├── algorithm/
│   ├── space/
│   └── target/
│
├── config/
│   └── ConfigManager
│
├── keybinds/
│   ├── Bind
│   ├── Key
│   └── Manager
│
├── Compositor
└── main
```

### Window management

Windows are tracked using XCB window IDs.

Geometry is calculated internally and then applied to X11 using XCB.

### Layout

Vexland contains a layout system with targets, spaces and algorithms.

The current layout implementation is based around a dwindle-style tiling
algorithm.

### Input

Keybind handling uses XKB/xkbcommon rather than hard-coded keycodes.

### Monitors

Monitor information is obtained through XRandR using the XCB RandR
extension.

### Visual effects

Vexland itself focuses on window management and geometry.

Visual effects such as:

- blur
- shadows
- rounded corners
- fading

are handled by picom.

## Current status

Vexland is experimental and actively developed.

The core architecture is in place, but the project is not yet a complete
replacement for a mature window manager.

Some functionality is still being implemented, including:

- Mouse input / drag & resize
- VT switching
- More complete workspace handling
- Window rules
- Groups / swallow
- Wobble effects
- More complete configuration support

Expect breaking changes.

## Releases

Prebuilt binaries are available on the GitHub Releases page:

https://github.com/MaybeWeDead/vexland/releases

Current releases may include builds for different architectures.

If a prebuilt binary does not work on your system, building from source is
recommended.

## Why Vexland?

Hyprland is a Wayland compositor.

Vexland exists for people who want a similar dynamic tiling workflow while
remaining on the X11/Xorg stack.

The goal is not to copy Hyprland's implementation. Vexland is an independent
X11 implementation inspired by the same style of window management.

## Contributing

Issues, ideas and pull requests are welcome.

If you find something that does not work:

1. Check whether it is already reported.
2. Open an issue with your X11 environment and relevant logs.
3. Include reproduction steps if possible.

## Disclaimer

Vexland is experimental software.

It may crash, break your session, or behave incorrectly.

Do not use it as your only window manager until you are comfortable recovering
your X11 session.

## License

See the repository for license information.
