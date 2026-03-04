#if !defined(IMG_NATIVE)

#include "ImageDecoder.h"

#if defined(__clang__) || defined(__GNUC__)
#warning "IMG plugin: decoder backend is not implemented; building with placeholder decoder factory."
#endif

void CreateCrossPlatformDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders)
{
	(void)decoders;
}

#endif // !IMG_NATIVE
