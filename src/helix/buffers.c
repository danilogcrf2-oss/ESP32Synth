#include <stdlib.h>
#include <string.h>
#include "coder.h"
#include "esp_heap_caps.h"

static inline __attribute__((always_inline)) void ClearBuffer(void *buf, int nBytes)
{
    // Xtensa highly optimizes memset using 32-bit hardware stores
    memset(buf, 0, nBytes);
}

MP3DecInfo *AllocateBuffers(void)
{
    MP3DecInfo *mp3DecInfo;

    // Force allocation in ultra-fast INTERNAL RAM and 16-byte align for SIMD/DMA safety
    mp3DecInfo = (MP3DecInfo *)heap_caps_aligned_alloc(16, sizeof(MP3DecInfo), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!mp3DecInfo) return 0;
    ClearBuffer(mp3DecInfo, sizeof(MP3DecInfo));
    
    FrameHeader *fh      = (FrameHeader *)    heap_caps_aligned_alloc(16, sizeof(FrameHeader), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    SideInfo *si         = (SideInfo *)       heap_caps_aligned_alloc(16, sizeof(SideInfo), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ScaleFactorInfo *sfi = (ScaleFactorInfo *)heap_caps_aligned_alloc(16, sizeof(ScaleFactorInfo), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    HuffmanInfo *hi      = (HuffmanInfo *)    heap_caps_aligned_alloc(16, sizeof(HuffmanInfo), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    DequantInfo *di      = (DequantInfo *)    heap_caps_aligned_alloc(16, sizeof(DequantInfo), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    IMDCTInfo *mi        = (IMDCTInfo *)      heap_caps_aligned_alloc(16, sizeof(IMDCTInfo), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    SubbandInfo *sbi     = (SubbandInfo *)    heap_caps_aligned_alloc(16, sizeof(SubbandInfo), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    mp3DecInfo->FrameHeaderPS     = (void *)fh;
    mp3DecInfo->SideInfoPS        = (void *)si;
    mp3DecInfo->ScaleFactorInfoPS = (void *)sfi;
    mp3DecInfo->HuffmanInfoPS     = (void *)hi;
    mp3DecInfo->DequantInfoPS     = (void *)di;
    mp3DecInfo->IMDCTInfoPS       = (void *)mi;
    mp3DecInfo->SubbandInfoPS     = (void *)sbi;

    if (!fh || !si || !sfi || !hi || !di || !mi || !sbi) {
        FreeBuffers(mp3DecInfo);
        return 0;
    }

    ClearBuffer(fh,  sizeof(FrameHeader));
    ClearBuffer(si,  sizeof(SideInfo));
    ClearBuffer(sfi, sizeof(ScaleFactorInfo));
    ClearBuffer(hi,  sizeof(HuffmanInfo));
    ClearBuffer(di,  sizeof(DequantInfo));
    ClearBuffer(mi,  sizeof(IMDCTInfo));
    ClearBuffer(sbi, sizeof(SubbandInfo));

    return mp3DecInfo;
}

#define SAFE_FREE(x) { if (x) heap_caps_free(x); (x) = 0; }

void FreeBuffers(MP3DecInfo *mp3DecInfo)
{
    if (!mp3DecInfo) return;

    SAFE_FREE(mp3DecInfo->FrameHeaderPS);
    SAFE_FREE(mp3DecInfo->SideInfoPS);
    SAFE_FREE(mp3DecInfo->ScaleFactorInfoPS);
    SAFE_FREE(mp3DecInfo->HuffmanInfoPS);
    SAFE_FREE(mp3DecInfo->DequantInfoPS);
    SAFE_FREE(mp3DecInfo->IMDCTInfoPS);
    SAFE_FREE(mp3DecInfo->SubbandInfoPS);

    heap_caps_free(mp3DecInfo);
}