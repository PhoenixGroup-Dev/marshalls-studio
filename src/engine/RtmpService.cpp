#include "RtmpService.h"

#include <obs-module.h>
#include <obs.h>

#include <string>

namespace ms {

namespace {

struct ServiceData {
	std::string server;
	std::string key;
};

const char *svcName(void *)
{
	return "Marshalls RTMP";
}

void svcUpdate(void *data, obs_data_t *settings)
{
	auto *d = static_cast<ServiceData *>(data);
	d->server = obs_data_get_string(settings, "server");
	d->key = obs_data_get_string(settings, "key");
	// Trim whitespace pasted with the key/URL.
	auto trim = [](std::string &s) {
		while (!s.empty() && isspace(uint8_t(s.back())))
			s.pop_back();
		size_t i = 0;
		while (i < s.size() && isspace(uint8_t(s[i])))
			++i;
		s.erase(0, i);
	};
	trim(d->server);
	trim(d->key);
}

void *svcCreate(obs_data_t *settings, obs_service_t *)
{
	auto *d = new ServiceData;
	svcUpdate(d, settings);
	return d;
}

void svcDestroy(void *data)
{
	delete static_cast<ServiceData *>(data);
}

const char *svcUrl(void *data)
{
	return static_cast<ServiceData *>(data)->server.c_str();
}

const char *svcKey(void *data)
{
	return static_cast<ServiceData *>(data)->key.c_str();
}

const char *svcProtocol(void *data)
{
	const std::string &s = static_cast<ServiceData *>(data)->server;
	return s.rfind("rtmps://", 0) == 0 ? "RTMPS" : "RTMP";
}

const char *svcOutputType(void *)
{
	return "rtmp_output";
}

const char *svcConnectInfo(void *data, uint32_t type)
{
	auto *d = static_cast<ServiceData *>(data);
	if (type == OBS_SERVICE_CONNECT_INFO_SERVER_URL)
		return d->server.c_str();
	if (type == OBS_SERVICE_CONNECT_INFO_STREAM_KEY)
		return d->key.c_str();
	return nullptr;
}

bool svcCanConnect(void *data)
{
	auto *d = static_cast<ServiceData *>(data);
	return !d->server.empty() && !d->key.empty();
}

const char *kCodecs[] = {"h264", nullptr};
const char *kAudioCodecs[] = {"aac", nullptr};

const char **svcVideoCodecs(void *)
{
	return kCodecs;
}

const char **svcAudioCodecs(void *)
{
	return kAudioCodecs;
}

} // namespace

void registerRtmpService()
{
	obs_service_info info = {};
	info.id = kRtmpServiceId;
	info.get_name = svcName;
	info.create = svcCreate;
	info.destroy = svcDestroy;
	info.update = svcUpdate;
	info.get_url = svcUrl;
	info.get_key = svcKey;
	info.get_protocol = svcProtocol;
	info.get_output_type = svcOutputType;
	info.get_connect_info = svcConnectInfo;
	info.can_try_to_connect = svcCanConnect;
	info.get_supported_video_codecs = svcVideoCodecs;
	info.get_supported_audio_codecs = svcAudioCodecs;
	obs_register_service(&info);
}

} // namespace ms
