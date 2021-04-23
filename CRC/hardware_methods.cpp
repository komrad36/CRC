#include <cstdint>
#include <immintrin.h>

// for these approaches, the poly CANNOT be changed, because these approaches
// use x86 hardware instructions which hardcode this poly internally.
static constexpr uint32_t P = 0x82f63b78U;

// OPTION 11
uint32_t option_11_hardware_1_byte(const void* M, uint32_t bytes)
{
    const uint8_t* M8 = (const uint8_t*)M;
    uint32_t R = 0;
    for (uint32_t i = 0; i < bytes; ++i)
    {
        R = _mm_crc32_u8(R, M8[i]);
    }
    return R;
}

// OPTION 12
uint32_t option_12_hardware_8_bytes(const void* M, uint32_t bytes)
{
    const uint64_t* M64 = (const uint64_t*)M;
    uint64_t R = 0;
    for (uint32_t i = 0; i < bytes >> 3; ++i)
    {
        R = _mm_crc32_u64(R, M64[i]);
    }
    return (uint32_t)R;
}
