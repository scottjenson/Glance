project('edge-shrink', 'cpp',
  version: '0.1.0',
  license: 'MIT',
  default_options: ['cpp_std=c++17', 'warning_level=2'])

# Wayfire's headers and the libraries it depends on (wlroots, wf-config, ...)
wayfire = dependency('wayfire')

# Wayfire's headers include GLM, but wayfire-devel doesn't pull it in.
# If this fails: sudo dnf install glm-devel
meson.get_compiler('cpp').has_header('glm/vec4.hpp', required: true)

# The shared "move drag" helper that Wayfire's own move plugin uses.
# Fedora's wayfire-devel ships it as a static library.
libdir = wayfire.get_variable(pkgconfig: 'libdir')
move_drag = meson.get_compiler('cpp').find_library(
  'wayfire-move-drag-interface', dirs: libdir, static: true)

# Produces build/libedgeshrink.so
shared_module('edgeshrink', 'edge-shrink.cpp',
  dependencies: [wayfire, move_drag],
  cpp_args: ['-DWLR_USE_UNSTABLE'],
  install: false)
