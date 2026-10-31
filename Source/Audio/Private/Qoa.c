#include <stddef.h>

void* SltAudioQoaAllocate(size_t size);
void SltAudioQoaFree(void* block);

#define QOA_MALLOC(size) SltAudioQoaAllocate(size)
#define QOA_FREE(block) SltAudioQoaFree(block)
#define QOA_NO_STDIO
#define QOA_IMPLEMENTATION
#include "ThirdParty/qoa.h"
