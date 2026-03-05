#include "ImageDecoder.h"
#include "../Settings.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

// Platform-specific function declarations
#ifdef __APPLE__
void CreateMacDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
#endif
void CreateCrossPlatformDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
void CreateHeifDecoder(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
void CreateWebPDecoder(std::vector<std::unique_ptr<ImageDecoder>>& decoders);

// Platform-specific decoder creation
std::vector<std::unique_ptr<ImageDecoder>> DecoderFactory::CreateDecoders()
{
	std::vector<std::unique_ptr<ImageDecoder>> decoders;

	if (g_settings.NativeImplementation()) {
#ifdef __APPLE__
		CreateMacDecoders(decoders);
#else
		CreateCrossPlatformDecoders(decoders);
		CreateHeifDecoder(decoders);
		CreateWebPDecoder(decoders);
#endif
	} else {
		CreateCrossPlatformDecoders(decoders);
		CreateHeifDecoder(decoders);
		CreateWebPDecoder(decoders);
	}

	return decoders;
}

// Thread-safe global decoder cache for FindDecoder
static std::vector<std::unique_ptr<ImageDecoder>> s_decoders;
static std::mutex s_decoders_mutex;
static bool s_last_native_impl = true;
static bool s_initialized = false;

ImageDecoder* DecoderFactory::FindDecoder(const std::string& path)
{
	std::lock_guard<std::mutex> lock(s_decoders_mutex);
	
	if (!s_initialized || s_last_native_impl != g_settings.NativeImplementation()) {
		s_decoders = CreateDecoders();
		s_last_native_impl = g_settings.NativeImplementation();
		s_initialized = true;
	}

	std::string ext = ExifHelpers::GetExtension(path);
	if (ext.empty()) {
		return nullptr;
	}

	for (auto& decoder : s_decoders) {
		if (decoder && decoder->CanHandle(ext.c_str())) {
			return decoder.get();
		}
	}

	return nullptr;
}
