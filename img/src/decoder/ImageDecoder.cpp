#include "ImageDecoder.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

// Platform-specific function declarations
#ifdef __linux__
void CreateLinuxDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
#elif __APPLE__
void CreateMacDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
#endif

// Platform-specific decoder creation
std::vector<std::unique_ptr<ImageDecoder>> DecoderFactory::CreateDecoders()
{
	std::vector<std::unique_ptr<ImageDecoder>> decoders;

#ifdef __linux__
	CreateLinuxDecoders(decoders);
#elif __APPLE__
	CreateMacDecoders(decoders);
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
