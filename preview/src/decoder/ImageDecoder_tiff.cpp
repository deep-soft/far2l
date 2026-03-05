// preview/src/decoder/ImageDecoder_tiff.cpp

#include "ImageDecoder.h" // For ImageDecoder, Image, ExifHelpers
#include "ImageDecoder_tiff.h" // For TIFF decoder declaration
#include "external/stb_image_resize2.h" // For resizing

// Include libtiff headers
#include <tiff.h>
#include <tiffio.h> // For TIFF reading functions

#include <string>
#include <vector>
#include <algorithm> // For std::max
#include <cstring>   // For memcpy
#include <cstdio>    // For FILE operations if needed, though libtiff might not need direct file IO for buffers.

class TiffImageDecoder : public ImageDecoder {
public:
    const char* Name() const override { return "libtiff"; }

    bool CanHandle(const char* ext) const override {
        if (!ext) return false;
        // Case-insensitive comparison for "tiff" and "tif"
        return strcmp(ext, "tiff") == 0 || strcmp(ext, "tif") == 0;
    }

    bool Decode(const std::string& path, Image& out, int& orientation, int maxPixelSize) override {
        // Read EXIF orientation. libtiff may support EXIF, but for simplicity, use helper.
        // TIFF orientation handling can be complex and might require dedicated EXIF parsing.
        // For now, we default to 1 (normal) and rely on helper for JPEGs that might be embedded.
        orientation = ExifHelpers::ReadExifOrientation(path); 

        // Open the TIFF file
        TIFF* tif = TIFFOpen(path.c_str(), "r");
        if (!tif) {
            return false; // Failed to open TIFF file
        }

        uint32_t width, height;
        uint16_t photometric_interpretation;
        uint16_t bits_per_sample;
        uint16_t samples_per_pixel;
        uint16_t sample_format; // Can be integer, float, etc.
        uint32_t image_width, image_height;

        // Get image dimensions
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &image_width);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &image_height);
        width = image_width;
        height = image_height;

        // Get samples per pixel (e.g., 1 for grayscale, 3 for RGB, 4 for RGBA)
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel);
        // Get bits per sample (e.g., 8 bits per channel)
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);
        // Get photometric interpretation (e.g., RGB, Grayscale)
        TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric_interpretation);
        
        // Determine target dimensions for scaling
        int targetWidth = width;
        int targetHeight = height;
        if (maxPixelSize > 0 && (width > maxPixelSize || height > maxPixelSize)) {
            float scale = static_cast<float>(maxPixelSize) / static_cast<float>(std::max(width, height));
            targetWidth = static_cast<int>(width * scale);
            targetHeight = static_cast<int>(height * scale);
        }

        // We need to decode to RGB (3 channels) as per Image struct.
        // libtiff can output various formats. We'll aim for RGB.
        // For simplicity, we assume 8 bits per sample, integer format.
        // More complex TIFFs (e.g., floating point, different bit depths, palettes) would require more logic.
        
        uint32_t image_rows = height;
        uint32_t image_cols = width;
        uint32_t strip_size = TIFFstripSize(tif);
        uint32_t num_strips = TIFFNumberOfStrips(tif);

        // Allocate buffer for decoded RGB data.
        // libtiff's TIFFReadRGBAImageOriented can be used for RGBA,
        // or TIFFReadRGBAStrip for strip-based reading.
        // For simplicity, let's assume we're getting RGB data.
        // If samples_per_pixel is 4 (RGBA), we can convert to RGB.
        // If samples_per_pixel is 1 (Grayscale), we can convert to RGB.

        int channels_out = 3; // Target: RGB

        // Use TIFFReadRGBAImageOriented for RGBA output, then convert to RGB if needed.
        // This handles orientation, color space, and sample format.
        // It expects RGBA output, so we need to handle the conversion to RGB.
        // The buffer size will be width * height * 4 (RGBA).
        
        std::vector<uint8_t> rgba_buffer(image_width * image_height * 4); // RGBA
        
        // Read image into RGBA buffer. TIFFReadRGBAImageOriented handles many TIFF complexities.
        // It returns 1 on success, 0 on failure.
        if (!TIFFReadRGBAImageOriented(tif, image_width, image_height, rgba_buffer.data(), 0, 0)) {
             TIFFClose(tif);
             return false;
        }
        
        // Now convert RGBA data to RGB format for the Image struct.
        // This is similar to the HEIF conversion: skip alpha channel.
        out.Resize(targetWidth, targetHeight, channels_out); // target RGB

        if (targetWidth != width || targetHeight != height) {
            // Scaling is needed
            stbir_resize_uint8_linear(
                rgba_buffer.data(), width, height, 0, // Source: RGBA buffer
                (uint8_t*)out.Data(), targetWidth, targetHeight, 0, // Destination: out RGB data
                STBIR_RGB); // Resize expects STBIR_RGB and maps RGBA->RGB
        } else {
            // No scaling, copy RGBA to RGB by skipping alpha
            uint8_t* output_rgb_data = (uint8_t*)out.Data();
            const uint8_t* input_rgba_data = rgba_buffer.data();
            size_t input_stride = image_width * 4; // RGBA stride

            for (uint32_t y = 0; y < image_height; ++y) {
                for (uint32_t x = 0; x < image_width; ++x) {
                    // RGBA to RGB conversion: (R, G, B, A) -> (R, G, B) by skipping Alpha
                    output_rgb_data[y * targetWidth * 3 + x * 3 + 0] = input_rgba_data[y * input_stride + x * 4 + 0]; // R
                    output_rgb_data[y * targetWidth * 3 + x * 3 + 1] = input_rgba_data[y * input_stride + x * 4 + 1]; // G
                    output_rgb_data[y * targetWidth * 3 + x * 3 + 2] = input_rgba_data[y * input_stride + x * 4 + 2]; // B
                }
            }
        }

        TIFFClose(tif);
        return true;
    }
};

// Factory function to create a TIFF decoder instance
void CreateTiffDecoder(std::vector<std::unique_ptr<ImageDecoder>>& decoders) {
    decoders.push_back(std::make_unique<TiffImageDecoder>());
}
