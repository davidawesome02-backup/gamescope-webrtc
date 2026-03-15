# Gamescope-remote
### Testing project for streaming webrtc from gamescope

> Warning, this is testing code, dont expect it to be great yet. This project is designed to use libdata channel to forward data we get from gamescope's pipewire API to a accessable webrtc stream. This is made in the hope of inclusion in partydeck for remote game access, but could be used independantly.

### Build:

NEW! 

just run `git submodule update --init --recursive --depth 1;mkdir build; cd build; cmake ..; cmake --build . -j`
