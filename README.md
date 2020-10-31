# GNOME Screen Cast OBS Studio plugin

Small source plugin to use GNOME Screen Cast functionality as a source for [OBS
Studio][1].

GNOME Screen Cast works for all GNOME sessions regardless of the compositor
being used. Basically saying that the main purpose of this plugin is to capture
screens under Wayland sessions where the X11 capture falls short.

You need to have GNOME Screen Cast application installed as well as the
GStreamer pipewire elements. Make sure that in "GNOME Settings" you have
"Sharing -> Desktop Sharing" enabled.

[1]: https://obsproject.com/

## Getting Window ID

Currently there is no (known) way to obtain mutter's ID for a window other than through Looking Glass, to obtain the ID:
  1. Press `ALT+F2` in the window that appears and type `lg` (Looking Glass)
  2. Click on the "Windows" tab in the upper right corner
  3. Select the *Window Name* at the top (Not the "App" section)
  4. Press "Insert" in the upper right corner
  5. Go back to the "Evaluator" tab and look at the last entry, it should have something like `r(n) = [...]`
  6. type `r(<number>).get_id()`

## Todo

Keep an eye on support for the Freedesktop variation of this API:

https://github.com/flatpak/xdg-desktop-portal/blob/master/data/org.freedesktop.portal.ScreenCast.xml

Once KDE/GNOME and distributions have picked this up we could modify the plugin
slightly to have unified platform support.

See: https://gitlab.gnome.org/feaneron/obs-xdg-portal/


## Build

Refer to the `Dockerfile` and `.gitlab-ci.yml` files on how to get a
development workspace and how to build the plugin.

### Fedora build
requirements include `gstreamer1-plugins-base-devel ghc-gi-gio-devel.x86_64 obs-studio-libs obs-studio-devel`. Install with `dnf install`
