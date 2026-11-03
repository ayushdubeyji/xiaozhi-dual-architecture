#!/usr/bin/env bash
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/run/user/1000
export SDL_VIDEODRIVER=wayland
export SDL_TOUCH_MOUSE_EVENTS=0
export SDL_MOUSE_TOUCH_EVENTS=0
exec /home/ayush/imx519_cam_gui/build/imx519_cam_gui "$@"