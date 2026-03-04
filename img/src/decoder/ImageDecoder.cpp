#include "ImageDecoder.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

// Platform-specific function declarations
#if defined(IMG_NATIVE)
void CreateMacDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
#else
void CreateCrossPlatformDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
#endif

// Platform-specific decoder creation
std::vector<std::unique_ptr<ImageDecoder>> DecoderFactory::CreateDecoders()
{
	std::vector<std::unique_ptr<ImageDecoder>> decoders;

#if defined(IMG_NATIVE)
	CreateMacDecoders(decoders);
#else
	CreateCrossPlatformDecoders(decoders);
#endif

	return decoders;
}

// Thread-safe global decoder cache for FindDecoder
static std::vector<std::unique_ptr<ImageDecoder>> s_decoders;
static std::once_flag s_decoders_once;

static void InitDecoders()
{
	s_decoders = DecoderFactory::CreateDecoders();
}

ImageDecoder* DecoderFactory::FindDecoder(const std::string& path)
{
	std::call_once(s_decoders_once, InitDecoders);

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
