#include <cstdint>

constexpr uint32_t P = 0x82f63b78;

uint32_t option_5_naive_cpp(const void* M, uint32_t bytes) {
	const uint8_t* M8 = (const uint8_t*)M;
	uint32_t R = 0;
	for (uint32_t i = 0; i < bytes; ++i) {
		R ^= M8[i];
		for (uint32_t j = 0; j < 8; ++j) {
			R = R & 1 ? (R >> 1) ^ P : R >> 1;
		}
	}
	return R;
}
