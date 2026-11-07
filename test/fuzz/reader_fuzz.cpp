#include "reader_fuzz.h"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const *data, size_t size)
{
	reader_fuzz::check_input(size ? std::string_view(reinterpret_cast<char const *>(data), size) : std::string_view{});
	return 0;
}
