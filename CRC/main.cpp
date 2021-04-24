#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>

static constexpr bool kPrintTables = false;

using namespace std::chrono;

void tabular_method_table_print_demo();
void golden_lut_print_demo_intel();
void golden_lut_print_demo_amd();

extern "C"
{
    uint32_t option_1_cf_jump(const void* M, uint32_t bytes);
    uint32_t option_2_multiply_mask(const void* M, uint32_t bytes);
    uint32_t option_3_bit_mask(const void* M, uint32_t bytes);
    uint32_t option_4_cmove(const void* M, uint32_t bytes);
}

uint32_t option_5_naive_cpp(const void* M, uint32_t bytes);

uint32_t option_6_tabular_1_byte(const void* M, uint32_t bytes);
uint32_t option_7_tabular_2_bytes(const void* M, uint32_t bytes);
uint32_t option_8_tabular_4_bytes(const void* M, uint32_t bytes);
uint32_t option_9_tabular_8_bytes(const void* M, uint32_t bytes);
uint32_t option_10_tabular_16_bytes(const void* M, uint32_t bytes);

uint32_t option_11_hardware_1_byte(const void* M, uint32_t bytes);
uint32_t option_12_hardware_8_bytes(const void* M, uint32_t bytes);

uint32_t option_13_golden_intel(const void* M, uint32_t bytes, uint32_t prev = 0);
uint32_t option_14_golden_amd(const void* M, uint32_t bytes, uint32_t prev = 0);

int main()
{
    if (kPrintTables)
    {
        tabular_method_table_print_demo();
        golden_lut_print_demo_intel();
        golden_lut_print_demo_amd();
    }

    constexpr size_t seed = 5;
    std::mt19937 gen(seed);
    //std::random_device rd;
    //std::mt19937 gen(rd);
    std::uniform_int_distribution<> dis(0, 255);

    struct TestItem
    {
        const char* m_name;
        union
        {
            uint32_t(*m_fNoPrev)(const void*, uint32_t);
            uint32_t(*m_fPrev)(const void*, uint32_t, uint32_t);
        };
        size_t m_runs;
        bool m_hasPrev;

        TestItem(const char* name, uint32_t(*fNoPrev)(const void*, uint32_t), size_t runs) :
            m_name(name),
            m_fNoPrev(fNoPrev),
            m_runs(runs),
            m_hasPrev(false)
        {
        }

        TestItem(const char* name, uint32_t(*fPrev)(const void*, uint32_t, uint32_t), size_t runs) :
            m_name(name),
            m_fPrev(fPrev),
            m_runs(runs),
            m_hasPrev(true)
        {
        }
    };

    printf("\nGenerating test data...\n");

    constexpr size_t kBytes = 900 * 1024;
    uint8_t* M = new uint8_t[kBytes];

    for (size_t i = 0; i < kBytes; ++i)
    {
        M[i] = dis(gen);
    }

    printf("Starting tests...\n\n");

    printf("--------------------------------|------------|---------------------------------\n");
    printf(" Option                         | Result     | Performance\n");
    printf("--------------------------------|------------|---------------------------------\n");

    TestItem items[] = {
        TestItem("Option 1:  Naive    - CF Jump ",	option_1_cf_jump,			    20),
        TestItem("Option 2:  Naive    - Mul Mask",	option_2_multiply_mask,		    35),
        TestItem("Option 3:  Naive    - Bit Mask",	option_3_bit_mask,			    45),
        TestItem("Option 5:  Naive    - CPP     ",	option_5_naive_cpp,			    60),
        TestItem("Option 4:  Naive    - Cmove   ",	option_4_cmove,				    60),
        TestItem("Option 6:  Tabular  - 1 byte  ",	option_6_tabular_1_byte,	    180),
        TestItem("Option 7:  Tabular  - 2 bytes ",	option_7_tabular_2_bytes,	    300),
        TestItem("Option 11: Hardware - 1 byte  ",	option_11_hardware_1_byte,	    500),
        TestItem("Option 8:  Tabular  - 4 bytes ",	option_8_tabular_4_bytes,	    600),
        TestItem("Option 9:  Tabular  - 8 bytes ",	option_9_tabular_8_bytes,	    1100),
        TestItem("Option 10: Tabular  - 16 bytes",	option_10_tabular_16_bytes,	    1500),
        TestItem("Option 12: Hardware - 8 bytes ",	option_12_hardware_8_bytes,	    5000),
        TestItem("Option 14: Golden   - AMD     ",	option_14_golden_amd,		    9000),
        TestItem("Option 13: Golden   - Intel   ",	option_13_golden_intel,		    10000),
    };

    for (const TestItem& item : items)
    {
        uint32_t result = 0;
        auto start = high_resolution_clock::now();
        if (item.m_hasPrev)
        {
            for (size_t i = 0; i < item.m_runs; ++i)
                result = item.m_fPrev(M, kBytes, 0);
        }
        else
        {
            for (size_t i = 0; i < item.m_runs; ++i)
                result = item.m_fNoPrev(M, kBytes);
        }

        auto end = high_resolution_clock::now();
        const double ns = (double)(duration_cast<nanoseconds>(end - start).count()) / item.m_runs;

        // approximating CPU clock as 4 GHz
        printf(" %s | 0x%08x | %7.1f MB/s | %.2f bits/cycle\n", item.m_name, result, kBytes / ns * 1e3, 2 * kBytes / ns);
    }

    printf("--------------------------------|------------|---------------------------------\n");
    printf("\nDone.\n\n");

    delete[] M;
}
