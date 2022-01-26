#
# obs-gnome-mutter-screencast. OBS Studio source plugin.
# Copyright (C) 2019-2020 Florian Zwoch <fzwoch@gmail.com>
#
# This file is part of obs-gnome-mutter-screencast.
#
# obs-gnome-mutter-screencast is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# obs-gnome-mutter-screencast is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with obs-gnome-mutter-screencast. If not, see <http://www.gnu.org/licenses/>.
#

project('gnome-mutter-screencast', 'c')

shared_library('gnome-mutter-screencast',
  'gnome-mutter-screencast.c',
  name_prefix : '',
  dependencies : [
    meson.get_compiler('c').find_library('obs'),
    dependency('gio-2.0'),
    dependency('gstreamer-1.0'),
    dependency('gstreamer-app-1.0'),
    dependency('gstreamer-video-1.0'),
    dependency('json-glib-1.0'),
  ],
  install : true,
  install_dir : join_paths(get_option('libdir'), 'obs-plugins'),
)
shared_library('cinnamon-muffin-screencast',
  'cinnamon-muffin-screencast.c',
  name_prefix : '',
  dependencies : [
    meson.get_compiler('c').find_library('obs'),
    dependency('gio-2.0'),
    dependency('gstreamer-1.0'),
    dependency('gstreamer-app-1.0'),
    dependency('gstreamer-video-1.0'),
    dependency('json-glib-1.0'),
  ],
  install : true,
  install_dir : join_paths(get_option('libdir'), 'obs-plugins'),
)
