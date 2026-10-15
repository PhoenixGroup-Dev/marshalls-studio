// Minimal RTMP/RTMPS service registered by the application.
//
// Replaces the OBS "rtmp-services" plugin, which downloads a remote service
// catalogue at startup and crashed on unload in the tested build (double free
// in obs_module_unload). Platform presets live in Planning.cpp instead.
#pragma once

namespace ms {
constexpr const char *kRtmpServiceId = "marshalls_rtmp";
void registerRtmpService();
} // namespace ms
