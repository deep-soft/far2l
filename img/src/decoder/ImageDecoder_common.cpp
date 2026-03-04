#include "ImageDecoder.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace ExifHelpers {

std::string GetExtension(const std::string& path)
{
	size_t dot = path.rfind('.');
	if (dot == std::string::npos || dot + 1 >= path.size()) {
		return "";
	}

	// Check if the dot is part of a directory path (e.g., /path.to/file)
	size_t slash = path.rfind('/');
	if (slash != std::string::npos && slash > dot) {
		return "";
	}

	std::string ext = path.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return ext;
}

bool ExtensionMatches(const std::string& ext, const char* list)
{
	if (ext.empty() || !list) {
		return false;
	}

	std::string lowerList = list;
	std::transform(lowerList.begin(), lowerList.end(), lowerList.begin(),
		[](unsigned char c) { return std::tolower(c); });

	// Parse space-separated wildcards like "*.jpg *.jpeg"
	std::string token;
	for (size_t i = 0; i <= lowerList.size(); ++i) {
		char c = (i < lowerList.size()) ? lowerList[i] : ' ';
		if (c == ' ' || c == '\t') {
			if (!token.empty()) {
				// Token is like "*.jpg" - extract "jpg"
				size_t star = token.find('*');
				size_t dot = token.find('.');
				if (star != std::string::npos && dot != std::string::npos && dot > star) {
					std::string pattern_ext = token.substr(dot + 1);
					if (pattern_ext == ext) {
						return true;
					}
				} else if (token == ext) {
					return true;
				}
				token.clear();
			}
		} else {
			token += c;
		}
	}
	return false;
}

// Read EXIF orientation from JPEG file
// Returns orientation value 1-8, or 1 if not found/not applicable
int ReadExifOrientation(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) {
		return 1;
	}

	// JPEG markers
	unsigned char buf[8];
	if (fread(buf, 1, 2, f) != 2 || buf[0] != 0xFF || buf[1] != 0xD8) {
		fclose(f);
		return 1; // Not a JPEG
	}

	// Search for EXIF APP1 marker
	while (true) {
		if (fread(buf, 1, 2, f) != 2) {
			fclose(f);
			return 1; // EOF
		}
		if (buf[0] != 0xFF) {
			fclose(f);
			return 1; // Invalid JPEG
		}

		unsigned char marker = buf[1];
		if (marker == 0xE1) { // APP1 - EXIF
			// Read segment length
			if (fread(buf, 1, 2, f) != 2) {
				fclose(f);
				return 1;
			}
			int seg_len = (buf[0] << 8) | buf[1];

			// Read EXIF header
			if (seg_len < 8 || fread(buf, 1, 6, f) != 6) {
				fclose(f);
				return 1;
			}
			seg_len -= 8; // Account for length bytes and header

			// Check for "Exif\0\0"
			if (memcmp(buf, "Exif\0\0", 6) != 0) {
				// Skip this segment
				fseek(f, seg_len, SEEK_CUR);
				continue;
			}

			// Read TIFF header
			unsigned char tiff_header[8];
			if (fread(tiff_header, 1, 8, f) != 8) {
				fclose(f);
				return 1;
			}

			bool little_endian = (tiff_header[0] == 0x49); // "II" = little endian
			auto read16 = [little_endian](const unsigned char* p) -> uint16_t {
				return little_endian ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]);
			};
			auto read32 = [little_endian](const unsigned char* p) -> uint32_t {
				return little_endian
					? (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24))
					: ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
			};

			uint32_t ifd_offset = read32(tiff_header + 4);
			if (ifd_offset < 8) {
				fclose(f);
				return 1;
			}

			// Seek to IFD0
			if (fseek(f, ifd_offset + 8 - 8, SEEK_CUR) != 0) {
				fclose(f);
				return 1;
			}

			// Read IFD0 entry count
			unsigned char count_buf[2];
			if (fread(count_buf, 1, 2, f) != 2) {
				fclose(f);
				return 1;
			}
			int entry_count = read16(count_buf);

			// Search for Orientation tag (0x0112)
			for (int i = 0; i < entry_count; ++i) {
				unsigned char entry[12];
				if (fread(entry, 1, 12, f) != 12) {
					fclose(f);
					return 1;
				}
				uint16_t tag = read16(entry);
				if (tag == 0x0112) { // Orientation
					uint16_t type = read16(entry + 2);
					uint32_t count = read32(entry + 4);
					if (type == 3 && count == 1) { // SHORT type
						uint16_t value = read16(entry + 8);
						fclose(f);
						return (value >= 1 && value <= 8) ? value : 1;
					}
				}
			}

			fclose(f);
			return 1; // Orientation tag not found
		} else if (marker == 0xD9) { // EOI
			fclose(f);
			return 1;
		} else if (marker == 0x00 || marker == 0xFF) {
			// Invalid or padding, continue
		} else if (marker >= 0xD0 && marker <= 0xD7) {
			// RST markers, no length
		} else if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8) {
			// SOF markers, have length but we're looking for EXIF
			if (fread(buf, 1, 2, f) != 2) {
				fclose(f);
				return 1;
			}
			int seg_len = (buf[0] << 8) | buf[1];
			fseek(f, seg_len - 2, SEEK_CUR);
		} else {
			// Other markers with length
			if (fread(buf, 1, 2, f) != 2) {
				fclose(f);
				return 1;
			}
			int seg_len = (buf[0] << 8) | buf[1];
			if (seg_len < 2) {
				fclose(f);
				return 1;
			}
			fseek(f, seg_len - 2, SEEK_CUR);
		}
	}
}

} // namespace ExifHelpers
