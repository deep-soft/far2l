#ifdef __linux__

#include "ImageDecoder.h"

#if defined(__clang__) || defined(__GNUC__)
#warning "IMG plugin: Linux decoder backend is not implemented; building with placeholder decoder factory."
#endif

void CreateLinuxDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders)
{
	(void)decoders;
}

#endif // __linux__
