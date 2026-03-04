#if !defined(IMG_NATIVE)

#include "ImageDecoder.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// Detection of CPU architecture for SIMD acceleration
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
	#define STBI_NEON
	#define STBIR_NEON
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
	#define STBI_SSE2
	#define STBIR_SSE2
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "external/stb_image_resize2.h"

#include "../Image.h"

class StbImageDecoder : public ImageDecoder {
public:
	const char* Name() const override { return "stb_image"; }

	bool CanHandle(const char* ext) const override
	{
		static const char* supported[] = {
			"jpg", "jpeg", "png", "bmp", "tga", "psd", "gif", "hdr", "pic", "pnm"
		};
		for (const auto& s : supported) {
			if (strcmp(ext, s) == 0) return true;
		}
		return false;
	}

	bool Decode(const std::string& path, Image& out, int& orientation, int maxPixelSize) override
	{
		int width, height, channels;
		orientation = ExifHelpers::ReadExifOrientation(path);

		// Load image info first to check dimensions
		if (!stbi_info(path.c_str(), &width, &height, &channels)) {
			return false;
		}

		// Calculate scaled dimensions if maxPixelSize is specified
		int targetWidth = width;
		int targetHeight = height;
		if (maxPixelSize > 0 && (width > maxPixelSize || height > maxPixelSize)) {
			float scale = (float)maxPixelSize / (float)std::max(width, height);
			targetWidth = (int)(width * scale);
			targetHeight = (int)(height * scale);
		}

		// Load the image. stb_image can convert to RGB (3 channels) automatically.
		unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 3);
		if (!data) return false;

		if (targetWidth != width || targetHeight != height) {
			// Resize using stb_image_resize2
			Image resized(targetWidth, targetHeight, 3);
			stbir_resize_uint8_linear(data, width, height, 0,
			                          (unsigned char*)resized.Data(), targetWidth, targetHeight, 0,
			                          STBIR_RGB);
			stbi_image_free(data);
			out = std::move(resized);
		} else {
			// Copy data into Image
			Image loaded(width, height, 3);
			memcpy(loaded.Data(), data, width * height * 3);
			stbi_image_free(data);
			out = std::move(loaded);
		}

		return true;
	}
};

void CreateCrossPlatformDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders)
{
	decoders.push_back(std::make_unique<StbImageDecoder>());
}

#endif // !IMG_NATIVE
