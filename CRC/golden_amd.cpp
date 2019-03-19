#include <cstdint>
#include <cstdio>
#include <immintrin.h>

constexpr int32_t P = 0x82f63b78;

static constexpr uint32_t g_lut_amd[] = {
0x00000001, 0x493c7d27, 0xf20c0dfe, 0xba4fc28e, 0x3da6d0cb, 0xddc0152b, 0x1c291d04, 0x9e4addf8,
0x740eef02, 0x39d3b296, 0x083a6eec, 0x0715ce53, 0xc49f4f67, 0x47db8317, 0x2ad91c30, 0x0d3b6092,
0x6992cea2, 0xc96cfdc0, 0x7e908048, 0x878a92a7, 0x1b3d8f29, 0xdaece73e, 0xf1d0f55e, 0xab7aff2a,
0xa87ab8a8, 0x2162d385, 0x8462d800, 0x83348832, 0x71d111a8, 0x299847d5, 0xffd852c6, 0xb9e02b86,
0xdcb17aa4, 0x18b33a4e, 0xf37c5aee, 0xb6dd949b, 0x6051d5a2, 0x78d9ccb7, 0x18b0d4ff, 0xbac2fd7b,
0x21f3d99c, 0xa60ce07b, 0x8f158014, 0xce7f39f4, 0xa00457f7, 0x61d82e56, 0x8d6d2c43, 0xd270f1a2,
0x00ac29cf, 0xc619809d, 0xe9adf796, 0x2b3cac5d, 0x96638b34, 0x65863b64, 0xe0e9f351, 0x1b03397f,
0x9af01f2d, 0xebb883bd, 0x2cff42cf, 0xb3e32c28, 0x88f25a3a, 0x064f7f26, 0x4e36f0b0, 0xdd7e3b0c,
0xbd6f81f8, 0xf285651c, 0x91c9bd4b, 0x10746f3c, 0x885f087b, 0xc7a68855, 0x4c144932, 0x271d9844,
0x52148f02, 0x8e766a0c, 0xa3c6f37a, 0x93a5f730, 0xd7c0557f, 0x6cb08e5c, 0x63ded06a, 0x6b749fb2,
0x4d56973c, 0x1393e203, 0x9669c9df, 0xcec3662e, 0xe417f38a, 0x96c515bb, 0x4b9e0f71, 0xe6fc4e6a,
0xd104b8fc, 0x8227bb8a, 0x5b397730, 0xb0cd4768, 0xe78eb416, 0x39c7ff35, 0x61ff0e01, 0xd7a4825c,
0x8d96551c, 0x0ab3844b, 0x0bf80dd2, 0x0167d312, 0x8821abed, 0xf6076544, 0x6a45d2b2, 0x26f6a60a,
0xd8d26619, 0xa741c1bf, 0xde87806c, 0x98d8d9cb, 0x14338754, 0x49c3cc9c, 0x5bd2011f, 0x68bce87a,
0xdd07448e, 0x57a3d037, 0xdde8f5b9, 0x6956fc3b, 0xa3e3e02c, 0x42d98888, 0xd73c7bea, 0x3771e98f,
0x80ff0093, 0xb42ae3d9, 0x8fe4c34d, 0x2178513a, 0xdf99fc11, 0xe0ac139e, 0x6c23e841, 0x170076fa,
};

void compute_golden_lut_amd(uint32_t* tbl, uint32_t n) {
	uint32_t R = 1;
	for (uint32_t i = 0; i < n << 1; ++i) {
		tbl[i] = R;
		for (uint32_t j = 0; j < 64; j++) {
			R = R & 1 ? (R >> 1) ^ P : R >> 1;
		}
	}
}

void print_golden_lut_amd(uint32_t* tbl, uint32_t n) {
	printf("static constexpr uint32_t g_lut_amd[] = {\n");
	for (uint32_t i = 0; i < n; ++i) {
		printf("0x%08x,%c", tbl[i], (i & 7) == 7 ? '\n' : ' ');
	}
	printf("};\n");
}

void golden_lut_print_demo_amd() {
	constexpr uint32_t n = 128;
	uint32_t* tbl = new uint32_t[n << 1];
	compute_golden_lut_amd(tbl, n);
	print_golden_lut_amd(tbl, n);
	delete tbl;
}

#define CRC_ITER(i) case i:								\
crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA - 8*(i)));	\
crcB = _mm_crc32_u64(crcB, *(uint64_t*)(pB - 8*(i)));

#define X0(n) CRC_ITER(n);
#define X1(n) X0(n+1) X0(n)
#define X2(n) X1(n+2) X1(n)
#define X3(n) X2(n+4) X2(n)
#define X4(n) X3(n+8) X3(n)
#define X5(n) X4(n+16) X4(n)
#define X6(n) X5(n+32) X5(n)
#define CRC_ITERS_128_TO_2() do {X0(128) X1(126) X2(122) X3(114) X4(98) X5(66) X6(2)} while(0)

// must be >= 16
constexpr uint32_t LEAF_SIZE_AMD = 7 * 16;

// OPTION 14
uint32_t option_14_golden_amd(const void* M, uint32_t bytes, uint32_t prev = 0) {
	uint64_t pA = (uint64_t)M;
	//uint64_t crcA = (uint64_t)(uint32_t)(~prev); // if you want to invert prev
	uint64_t crcA = prev;
	uint32_t toAlign = ((uint64_t)-(int64_t)pA) & 7;

	for (; toAlign && bytes; ++pA, --bytes, --toAlign)
		crcA = _mm_crc32_u8((uint32_t)crcA, *(uint8_t*)pA);

	while (bytes >= LEAF_SIZE_AMD) {
		const uint32_t n = bytes < 128 * 16 ? bytes >> 4 : 128;
		pA += 8 * n;
		uint64_t pB = pA + 8 * n;
		uint64_t crcB = 0;
		switch (n)
			CRC_ITERS_128_TO_2();

		crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA - 8));
		const __m128i vK = _mm_cvtsi32_si128(g_lut_amd[n - 1]);
		const __m128i vA = _mm_clmulepi64_si128(_mm_cvtsi64_si128(crcA), vK, 0);
		crcA = _mm_crc32_u64(crcB, _mm_cvtsi128_si64(vA) ^ *(uint64_t*)(pB - 8));

		bytes -= n << 4;
		pA = pB;
	}

	for (; bytes >= 8; bytes -= 8, pA += 8)
		crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA));

	for (; bytes; --bytes, ++pA)
		crcA = _mm_crc32_u8((uint32_t)crcA, *(uint8_t*)(pA));

	//return ~(uint32_t)crcA; // if you want to invert the result
	return (uint32_t)crcA;
}
