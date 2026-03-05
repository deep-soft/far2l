#pragma once
#include "ImageDecoder.h"
#include <vector>
#include <memory>

// Factory function declaration for TIFF decoder
void CreateTiffDecoder(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
