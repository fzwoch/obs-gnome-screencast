# GNOME Screen Cast OBS Studio plugin

Small source plugin to use GNOME Screen Cast functionality as a source for [OBS
Studio][1].

GNOME Screen Cast works for all GNOME sessions regardless of the compositor
being used. Basically saying that the main purpose of this plugin is to capture
screens under Wayland sessions where the X11 capture falls short.

The implementation is kind of weird. Not sure if there are better ways - but the
GNOME Screen Cast normally insist of writing to a file. Here we trick the system
to write to a SHM socket instead which we then read and pass to OBS.

I'm not sure how latency or audio/video sync is handled by OBS. But as soon as
a frame is read it gets a time stamp of `os_gettime_ns()` which felt like the
best bet for a live source.

[1]: https://obsproject.com/

## Known issues

GNOME Screen Cast seems to be limited to a single concurrent session.

The mouse cursor is currently very choppy. The reason for this is that the
cursor drawing is done at the GNOME Screen Cast implementation but they only
update cursor coordinates every 100ms.

Not enough error checking and reporting. If it fails for you you probably have
to do some debugging by yourself.

Not very well tested. I just clicked around a bit to check it seems working in
general. But multi monitor setups etc. are coded by best guess - but have not
been verified if they are actually working correctly.

## Build

I don't know CMake so you will have to deal with meson instead. Assuming that
all dependencies are installed correctly this should do the trick:

```
meson --buildtype=release build
ninja -C build
```

You will then have to copy the plugin to your OBS plugin directory.
