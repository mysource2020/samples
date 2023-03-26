#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>


//Check for SSE 4.2 of Intel hardware instruction
uint32_t hasSSE4_2(void)
{
    uint32_t have;
    do {
        uint32_t eax, ecx;
        eax = 1;
        __asm__("cpuid"
            : "=c"(ecx)
            : "a"(eax)
            : "%ebx", "%edx");
        (have) = (ecx >> 20) & 1;
    } while (0);


    return have;
}

/* Compute CRC-32C using the Intel hardware instruction. */
static uint32_t crc32c_hw(const void* buf, size_t len)
{
    const unsigned char* next = (unsigned char*)buf;
    const unsigned char* end;
    register uint64_t crc0 = 0xffffffff;

    end = next + (len - (len & 7));
    while (next < end) {
        __asm__("crc32q\t" "(%1), %0"
            : "=r"(crc0)
            : "r"(next), "0"(crc0));
        next += 8;
    }
    len &= 7;

    while (len) {
        __asm__("crc32b\t" "(%1), %0"
            : "=r"(crc0)
            : "r"(next), "0"(crc0));
        next++;
        len--;
    }

    return (uint32_t)crc0 ^ 0xffffffff;
}


int main()
{
    if (hasSSE4_2())
    {
        //simple
        char* sz = "www.sina.com.cn";
        int len = strlen(sz);
        uint32_t crc32 = crc32c_hw(sz, len);
        printf("crc32 is 0x%08x \n", crc32);

        //calc times: 1000000000 escape: 238 ms
        int count = 1000*1000*1000;
        struct timespec tsStart, tsEnd;
        clock_gettime(CLOCK_REALTIME, &tsStart);

        for (int i = 0; i < count; i++)
        {
            crc32 = crc32c_hw(sz, len);
        }

        clock_gettime(CLOCK_REALTIME, &tsEnd);
        uint32_t nEscaped = (tsEnd.tv_sec * 1000 + tsEnd.tv_nsec / 1000000) - (tsStart.tv_sec * 1000 + tsStart.tv_nsec / 1000000);
        printf("count: %d crc32:0x%08x escape: %d ms\n\n", count, crc32, nEscaped);
    }
    else
    {
        printf("SSE 4.2 no support. \n");
    }

    return 0;
}