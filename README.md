# Gamescope-remote
### Testing project for streaming webrtc from gamescope

> Warning, this is testing code, dont expect it to be great yet. This project is designed to use libdata channel to forward data we get from gamescope's pipewire API to a accessable webrtc stream. This is made in the hope of inclusion in partydeck for remote game access, but could be used independantly.

### Build:

After building libdatachanel, use cmake to build gamescope-remote.

> Reminder: Copy the `include/nlohmann/json.hpp` to the include directory of your prefix path.
```sh
cmake -B build -DCMAKE_PREFIX_PATH=~/local/libdatachannel-install -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_BUILD_TYPE=Release
```

Compile time deps:
* Headers for runtime deps

Runtime deps:
* FFmpeg / libav 
    * libavformat
    * libavcodec
    * libavutil
    * libswscale
* libpipewire-0.3

> Note: you can compile to static ffmpeg, but code size goes up substancialy

To install env:
```sh
mkdir -P ~/local/libdatachannel-install

git clone https://github.com/paullouisageneau/libdatachannel.git

cd libdatachannel
git submodule update --init --recursive --depth 1

cmake -B build -DCMAKE_INSTALL_PREFIX=~/local/libdatachannel-install -DJSON_Install=ON

cmake --build build --target install

cp deps/json/single_include/nlohmann/json.hpp ~/local/libdatachannel-install/include/nlohmann/
```

```
~/local/libdatachannel-install
├── include
│   ├── nlohmann
│   │   └── json.hpp
│   └── rtc
│       ├── av1rtppacketizer.hpp
│       ├── candidate.hpp
│       ├── channel.hpp
│       ├── common.hpp
│       ├── configuration.hpp
│       ├── datachannel.hpp
│       ├── dependencydescriptor.hpp
│       ├── description.hpp
│       ├── frameinfo.hpp
│       ├── global.hpp
│       ├── h264rtpdepacketizer.hpp
│       ├── h264rtppacketizer.hpp
│       ├── h265nalunit.hpp
│       ├── h265rtpdepacketizer.hpp
│       ├── h265rtppacketizer.hpp
│       ├── iceudpmuxlistener.hpp
│       ├── mediahandler.hpp
│       ├── message.hpp
│       ├── nalunit.hpp
│       ├── pacinghandler.hpp
│       ├── peerconnection.hpp
│       ├── plihandler.hpp
│       ├── reliability.hpp
│       ├── rembhandler.hpp
│       ├── rtc.h
│       ├── rtc.hpp
│       ├── rtcpnackresponder.hpp
│       ├── rtcpreceivingsession.hpp
│       ├── rtcpsrreporter.hpp
│       ├── rtpdepacketizer.hpp
│       ├── rtp.hpp
│       ├── rtppacketizationconfig.hpp
│       ├── rtppacketizer.hpp
│       ├── track.hpp
│       ├── utils.hpp
│       ├── version.h
│       ├── websocket.hpp
│       └── websocketserver.hpp
└── lib
    ├── cmake
    │   └── LibDataChannel
    │       ├── LibDataChannelConfig.cmake
    │       ├── LibDataChannelConfigVersion.cmake
    │       ├── LibDataChannelTargets.cmake
    │       └── LibDataChannelTargets-release.cmake
    ├── libdatachannel.so -> libdatachannel.so.0.24
    ├── libdatachannel.so.0.24 -> libdatachannel.so.0.24.0
    └── libdatachannel.so.0.24.0
```