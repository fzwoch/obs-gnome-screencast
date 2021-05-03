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
