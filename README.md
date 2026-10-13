# xdg-desktop-portal-hyprland

An [XDG Desktop Portal](https://github.com/flatpak/xdg-desktop-portal) backend
for Hyprland.

> [!NOTE]
> This is a fork of
> [hyprwm/xdg-desktop-portal-hyprland](https://github.com/hyprwm/xdg-desktop-portal-hyprland)
> that adds the RemoteDesktop and Clipboard portals, so that remote desktop
> clients can control the desktop and share its clipboard. It's kept rebased on
> upstream's `master`.

## Branches

| Branch                   | Contents                                                                  |
| ------------------------ | ------------------------------------------------------------------------- |
| `master`                 | upstream `master`, with both of the branches below and this README        |
| `feature/remote-desktop` | the RemoteDesktop portal, on top of upstream `master`                     |
| `feature/clipboard`      | the Clipboard portal, on top of `feature/remote-desktop`, which it needs  |

### `feature/remote-desktop`

Implements `org.freedesktop.impl.portal.RemoteDesktop` (version 2):

- pointer and keyboard input, through the portal's own `Notify*` methods and
  through libei with `ConnectToEIS`. Touch isn't supported yet.
- sharing a screen in a remote desktop session, through the ScreenCast portal's
  `SelectSources`, with absolute pointer motion mapped onto the shared output
- keyboard input that follows the compositor's keymap and modifiers
- restoring sessions

It needs a compositor with `zwlr_virtual_pointer_manager_v1` and
`zwp_virtual_keyboard_manager_v1`, which Hyprland has.

### `feature/clipboard`

Implements `org.freedesktop.impl.portal.Clipboard`, for remote desktop sessions
that request the clipboard before they start. They can read the clipboard, get
told when it changes, and put their own content on it for apps to paste.

It shares the seat's clipboard through `ext_data_control_manager_v1`, which
Hyprland has. Without it, the clipboard isn't enabled for any session.

## Installing

First, make sure to install the required dependencies:

```
gbm
hyprland-protocols
hyprlang
hyprutils
hyprwayland-scanner
libdrm
libei
libpipewire-0.3
libspa-0.2
sdbus-cpp
wayland-client
wayland-protocols (>= 1.39)
xkbcommon
```

Then run the build and install command:

```sh
git clone --recursive https://github.com/spotdemo4/xdg-desktop-portal-hyprland
cd xdg-desktop-portal-hyprland/
cmake -DCMAKE_INSTALL_LIBEXECDIR=/usr/lib -DCMAKE_INSTALL_PREFIX=/usr -B build
cmake --build build
sudo cmake --install build
```

## Nix

> [!CAUTION]
> XDPH should not be used from this flake directly!
> 
> Instead, use it from the [Hyprland flake](https://github.com/hyprwm/Hyprland).

There are two reasons for the above:

1. Hyprland depends on XDPH, but XDPH also depends on Hyprland. This results in
   a cyclic dependency, which is a nightmare. To counter this, we use the
   Nixpkgs Hyprland package in this flake, so that it can be later consumed by
   the Hyprland flake while overriding the Hyprland package.
2. Even if you manually do all the overriding, you may still get it wrong and
   lose out on the Cachix cache (which has XDPH as exposed by the Hyprland
   flake).

## Running, FAQs, etc.

See
[the Hyprland wiki](https://wiki.hyprland.org/Hypr-Ecosystem/xdg-desktop-portal-hyprland/)
