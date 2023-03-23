#  This file is part of DirectFB.
#
#  This library is free software; you can redistribute it and/or
#  modify it under the terms of the GNU Lesser General Public
#  License as published by the Free Software Foundation; either
#  version 2.1 of the License, or (at your option) any later version.
#
#  This library is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with this library; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA

project('DirectFB2-eglgbm', 'c',
        version: '2.0.0')

directfb_dep = dependency('directfb-internal')

moduledir = directfb_dep.get_pkgconfig_variable('moduledir')

eglgbm_dep = [dependency('egl'), dependency('gbm'), dependency('glesv2'), dependency('libdrm')]

pkgconfig = import('pkgconfig')

eglgbm_sources = [
  'egl_layer.c',
  'egl_screen.c',
  'egl_surface_pool.c',
  'egl_system.c',
]

library('directfb_eglgbm',
        eglgbm_sources,
        dependencies: [directfb_dep, eglgbm_dep],
        install: true,
        install_dir: join_paths(moduledir, 'systems'))

pkgconfig.generate(filebase: 'directfb-system-eglgbm',
                   variables: 'moduledir=' + moduledir,
                   name: 'DirectFB-system-eglgbm',
                   description: 'GBM system module',
                   requires_private: ['egl', 'gbm', 'glesv2', 'libdrm'],
                   libraries_private: ['-L${moduledir}/systems',
                                       '-Wl,--whole-archive -ldirectfb_eglgbm -Wl,--no-whole-archive'])

if get_option('default_library') != 'shared'
  meson.add_install_script('dfb-update-pkgconfig', skip_if_destdir: true)
endif
