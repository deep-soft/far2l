#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../Image.h"

// EXIF orientation values (1-8)
// See: http://sylvana.net/jpegcrop/exif_orientation.html
enum class ExifOrientation {
	NORMAL = 1,
	FLIP_HORIZONTAL = 2,
	ROTATE_180 = 3,
	FLIP_VERTICAL = 4,
	TRANSPOSE = 5,      // Rotate 90 CW + flip horizontal
	ROTATE_90_CW = 6,
	TRANSVERSE = 7,     // Rotate 90 CCW + flip horizontal
	ROTATE_90_CCW = 8
};

class ImageDecoder {
public:
	virtual ~ImageDecoder() = default;

	// Decode image from file into RGB buffer
	// maxPixelSize: if > 0, decode at max this size (for fast thumbnail decode+scale)
	// Returns true on success, false on failure
	// orientation is set to EXIF orientation value if available (1-8), or 1 if not
	virtual bool Decode(const std::string& path, Image& out, int& orientation, int maxPixelSize = 0) = 0;

	// Check if this decoder can handle the given file extension
	virtual bool CanHandle(const char* ext) const = 0;

	// Human-readable decoder name for debugging
	virtual const char* Name() const = 0;
};

// Factory for creating platform-specific decoders
class DecoderFactory {
public:
	// Create all available decoders for the current platform
	static std::vector<std::unique_ptr<ImageDecoder>> CreateDecoders();

	// Find a decoder that can handle the given file
	static ImageDecoder* FindDecoder(const std::string& path);
};

// Common helper functions for EXIF orientation
namespace ExifHelpers {
	// Get file extension (lowercase, without dot)
	std::string GetExtension(const std::string& path);

	// Check if extension matches any in space-separated list
	bool ExtensionMatches(const std::string& ext, const char* list);

	// Read EXIF orientation from JPEG file (returns 1-8, or 1 if not found)
	int ReadExifOrientation(const std::string& path);
}
