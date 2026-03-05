#include "ImageDecoder.h"
#include "../PreviewLog.h"
#include <webp/decode.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#include "external/stb_image_resize2.h"

class WebPImageDecoder : public ImageDecoder {
public:
	const char* Name() const override { return "libwebp"; }

	bool CanHandle(const char* ext) const override
	{
		if (!ext) return false;
		return strcasecmp(ext, "webp") == 0;
	}

	bool Decode(const std::string& path, Image& out, int& orientation, int maxPixelSize) override
	{
		DBG("Decoding via libwebp: %s", path.c_str());
		orientation = 1; // WebP usually doesn't have EXIF orientation, or it's applied during decode

		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open()) return false;
		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<uint8_t> buffer(size);
		if (!file.read((char*)buffer.data(), size)) return false;

		int width, height;
		uint8_t* data = WebPDecodeRGB(buffer.data(), buffer.size(), &width, &height);
		if (!data) return false;

		int targetWidth = width;
		int targetHeight = height;
		if (maxPixelSize > 0 && (width > maxPixelSize || height > maxPixelSize)) {
			float scale = (float)maxPixelSize / (float)std::max(width, height);
			targetWidth = (int)(width * scale);
			targetHeight = (int)(height * scale);
		}

		if (targetWidth != width || targetHeight != height) {
			out.Resize(targetWidth, targetHeight, 3);
			stbir_resize_uint8_linear(data, width, height, width * 3,
			                          (unsigned char*)out.Data(), targetWidth, targetHeight, 0,
			                          STBIR_RGB);
		} else {
			out.Resize(width, height, 3);
			memcpy(out.Data(), data, width * height * 3);
		}

		WebPFree(data);
		return true;
	}
};

void CreateWebPDecoder(std::vector<std::unique_ptr<ImageDecoder>>& decoders)
{
	decoders.push_back(std::make_unique<WebPImageDecoder>());
}
