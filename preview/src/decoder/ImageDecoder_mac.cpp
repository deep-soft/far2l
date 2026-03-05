#ifdef __APPLE__

#include "ImageDecoder.h"
#include "../PreviewLog.h"
#include <cstdio>
#include <cstring>
#include <vector>

#include <ImageIO/ImageIO.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Accelerate/Accelerate.h>  // vImage for fast ARGB→RGB

// ============================================================================
// macOS ImageIO Decoder - optimized with thumbnail decode+scale
// ============================================================================
class MacOSImageDecoder : public ImageDecoder {
public:
	bool Decode(const std::string& path, Image& out, int& orientation, int maxPixelSize) override;
	bool CanHandle(const char* ext) const override;
	const char* Name() const override { return "ImageIO"; }

private:
	static CGImageRef LoadScaledImage(const char* path, int maxPixelSize);
	static bool ExtractRGB(CGImageRef image, Image& out);
};

bool MacOSImageDecoder::CanHandle(const char* ext) const
{
	if (!ext || !*ext) return false;

	const char* supported[] = {
		"jpg", "jpeg", "png", "gif", "webp", "heic", "heif", "tiff", "tif",
		"bmp", "ico", "cur", "xbm", "tga", "psd", "raw", "cr2", "nef", "dng",
		nullptr
	};

	for (const char** p = supported; *p; ++p) {
		if (strcasecmp(ext, *p) == 0) {
			return true;
		}
	}
	return false;
}

CGImageRef MacOSImageDecoder::LoadScaledImage(const char* path, int maxPixelSize)
{
	// Create URL from file path
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(
		nullptr,
		(const UInt8*)path,
		strlen(path),
		false
	);
	if (!url) return nullptr;

	// Create image source
	CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
	CFRelease(url);
	if (!src) return nullptr;

	CGImageRef img = nullptr;

	if (maxPixelSize > 0) {
		// Use thumbnail API for fast decode + scale in one pass
		int maxSide = maxPixelSize;
		CFNumberRef maxSizeNum = CFNumberCreate(nullptr, kCFNumberIntType, &maxSide);

		const void* keys[] = {
			kCGImageSourceCreateThumbnailFromImageAlways,
			kCGImageSourceThumbnailMaxPixelSize,
			kCGImageSourceCreateThumbnailWithTransform,
			kCGImageSourceShouldCacheImmediately
		};
		const void* values[] = {
			kCFBooleanTrue,
			maxSizeNum,
			kCFBooleanTrue,  // Apply EXIF orientation during decode
			kCFBooleanFalse  // Don't cache thumbnails immediately to save memory/time
		};

		CFDictionaryRef options = CFDictionaryCreate(
			nullptr, keys, values, 4,
			&kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks
		);
		CFRelease(maxSizeNum);

		img = CGImageSourceCreateThumbnailAtIndex(src, 0, options);
		CFRelease(options);
	}


	if (!img) {
		// Fallback: full decode (no scaling)
		img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
	}

	CFRelease(src);
	return img;
}

bool MacOSImageDecoder::ExtractRGB(CGImageRef image, Image& out)
{
	size_t width = CGImageGetWidth(image);
	size_t height = CGImageGetHeight(image);

	vImage_Buffer srcBuf{};
	vImage_CGImageFormat format = {
		.bitsPerComponent = 8,
		.bitsPerPixel = 32,
		.colorSpace = nullptr, // Default to sRGB
		.bitmapInfo = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Big,
		.version = 0,
		.decode = nullptr,
		.renderingIntent = kCGRenderingIntentDefault
	};

	vImage_Error err = vImageBuffer_InitWithCGImage(&srcBuf, &format, nullptr, image, kvImageNoFlags);
	if (err != kvImageNoError) {
		DBG("vImageBuffer_InitWithCGImage failed: %ld", (long)err);
		return false;
	}

	// RAII wrapper to ensure memory is freed even if we add early returns later
	struct VImageBufferAutoFree {
		vImage_Buffer* buf;
		~VImageBufferAutoFree() { if (buf && buf->data) free(buf->data); }
	} autoFree{&srcBuf};

	// Allocate output buffer (3 bytes per pixel for RGB)
	out.Resize(width, height, 3);

	vImage_Buffer dstBuf = {
		.data = (void*)out.Data(),
		.height = (vImagePixelCount)height,
		.width = (vImagePixelCount)width,
		.rowBytes = width * 3
	};

	// Convert ARGB to RGB
	err = vImageConvert_ARGB8888toRGB888(&srcBuf, &dstBuf, kvImageNoFlags);

	return err == kvImageNoError;
}

bool MacOSImageDecoder::Decode(const std::string& path, Image& out, int& orientation, int maxPixelSize)
{
	DBG("Decoding via Native ImageIO: %s", path.c_str());
	// Load image with optional scaling
	CGImageRef image = LoadScaledImage(path.c_str(), maxPixelSize);
	if (!image) {
		DBG("Native ImageIO failed to load image: %s", path.c_str());
		return false;
	}

	// Get orientation from image properties
	orientation = 1;
	// Note: When using kCGImageSourceCreateThumbnailWithTransform, orientation
	// is already applied, so we return 1 (normal)

	// Extract RGB data
	bool success = ExtractRGB(image, out);
	DBG("Native ImageIO extraction success=%d, size=%zux%zu", success, CGImageGetWidth(image), CGImageGetHeight(image));

	CGImageRelease(image);
	return success;
}

// ============================================================================
// Factory function
// ============================================================================
void CreateMacDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders)
{
	decoders.push_back(std::make_unique<MacOSImageDecoder>());
}

#endif // __APPLE__
