# GNOME Screen Cast OBS Studio plugin

Small source plugin to use GNOME Screen Cast functionality as a source for [OBS
Studio][1].

GNOME Screen Cast works for all GNOME sessions regardless of the compositor
being used. Basically saying that the main purpose of this plugin is to capture
screens under Wayland sessions where the X11 capture falls short.

The implementation is kind of weird. Not sure if there are better ways - but the
GNOME Screen Cast normally insist of writing to a file. Here we trick the system
to write to a SHM socket instead which we then read and pass to OBS.

I'm not sure how latency or audio/video sync is handled by OBS. ~~But as soon as
a frame is read it gets a time stamp of `os_gettime_ns()` which felt like the
best bet for a live source.~~ Since I have no idea what the master clock is each
frame gets a time stamp of it's frame number. OBS Studio seems smart enough to
do the right thing then.

[1]: https://obsproject.com/

## Getting Window ID

Currently there is no (knonw) way to obtain mutter's ID for a window other than through Looking Glass, to obtain the ID:
  1. Press `ALT+F2` in the window that appears and type `lg` (Looking Glass)
  2. Click on the "Windows" tab in the upper right corner
  3. Select the *Window Name* at the top (Not the "App" section)
  4. Press "Insert" in the upper right corner
  5. Go back to the "Evaluator" tab and look at the last entry, it should have something like `r(n) = [...]`
  6. type `r(<number>).get_id()`

## Known issues

GNOME Screen Cast seems to be limited to a single concurrent session.

The mouse cursor is currently very choppy. The reason for this is that the
cursor drawing is done at the GNOME Screen Cast implementation but they only
update cursor coordinates every 100ms.


## Todo

Keep an eye on support for the Freedesktop variation of this API:

https://github.com/flatpak/xdg-desktop-portal/blob/master/data/org.freedesktop.portal.ScreenCast.xml

Once KDE/GNOME and distributions have picked this up we could modify the plugin
slightly to have unified platform support.

## Build

I don't know CMake so you will have to deal with meson instead. Assuming that
all dependencies are installed correctly this should do the trick:

```shell
$ meson --buildtype=release build
$ ninja -C build

# optional; for installing the plugin into
# '/usr/local/lib/obs-plugins'

$ sudo ninja -C build install
```

### Dependencies

On Fedora, the following dependencies may need to be installed:

* meson (to build)
* libgnome-devel (gdk-3.0)
* gstreamer1-devel gstreamer1-plugins-base-devel (gstreamer-1.0)
