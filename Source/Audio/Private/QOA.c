#include <stddef.h>

void* SltAudioQOAAllocate(size_t size);
void SltAudioQOAFree(void* block);

#define QOA_MALLOC(size) SltAudioQOAAllocate(size)
#define QOA_FREE(block) SltAudioQOAFree(block)
#define QOA_NO_STDIO
#define QOA_IMPLEMENTATION
#include "ThirdParty/qoa.h"
