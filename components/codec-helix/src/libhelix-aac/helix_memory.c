#include <stdlib.h>
#include "esp_heap_caps.h"

void* helix_malloc(int size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = malloc(size);
    }
    return ptr;
}

void helix_free(void* ptr)
{
    if (ptr) {
        free(ptr);
    }
}
