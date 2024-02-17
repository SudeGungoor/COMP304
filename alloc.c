#include <stddef.h>
#include <unistd.h>
#include <string.h>
#define THRESHOLD_FOR_WORST_FIT 64  
#define MIN_SPLIT_SIZE 16            
#define BATCH_SIZE 1024              

typedef struct Block {
    size_t size;
    struct Block* next;
} Block;

static Block* head = NULL;


void* kumalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    // align size to the nearest multiple of sizeof(Block)
    size = (size + sizeof(Block) - 1) & ~(sizeof(Block) - 1);

    Block* bestBlock = NULL; // used for worst-fit
    Block* bestPrev = NULL;  // previous block for worst-fit

    Block* current = head;
    Block* prev = NULL;

    while (current != NULL) {
        if (current->size >= size) {
            // first-fit for small allocations
            if (size < THRESHOLD_FOR_WORST_FIT) {
                bestBlock = current;
                bestPrev = prev;
                break;
            }
            // worst-fit for larger allocations
            if (bestBlock == NULL || current->size > bestBlock->size) {
                bestBlock = current;
                bestPrev = prev;
            }
        }
        prev = current;
        current = current->next;
    }

    // if a suitable block is found
    if (bestBlock) {
        // remove block from free list
        if (bestPrev) {
            bestPrev->next = bestBlock->next;
        } else {
            head = bestBlock->next;
        }
        // check if the block can be split
        if (bestBlock->size > size + MIN_SPLIT_SIZE) {
            Block* remainingBlock = (Block*)((char*)bestBlock + sizeof(Block) + size);
            remainingBlock->size = bestBlock->size - size - sizeof(Block);
            remainingBlock->next = head;
            head = remainingBlock;

            bestBlock->size = size;
        }

        return (void*)(bestBlock + 1);
    }

    // allocate a new block if no suitable block is found in the free list
    size_t allocSize = size < BATCH_SIZE ? BATCH_SIZE : size;
    Block* newBlock = (Block*)sbrk(sizeof(Block) + allocSize);
    if (newBlock == (void*)-1) {
        return NULL; // sbrk failed
    }
    newBlock->size = size;

    // aplit the block
    if (allocSize > size) {
        Block* remainingBlock = (Block*)((char*)newBlock + sizeof(Block) + size);
        remainingBlock->size = allocSize - size - sizeof(Block);
        remainingBlock->next = head;
        head = remainingBlock;
    }

    return (void*)(newBlock + 1);
}

void *kucalloc(size_t nmemb, size_t size) {
    size_t totalSize = nmemb * size;
    void* ptr = kumalloc(totalSize);
    if (ptr != NULL) {
        // Zero-initialize the allocated block
        for (size_t i = 0; i < totalSize; ++i) {
            ((char*)ptr)[i] = 0;
        }
    }
    return ptr;
}
void kufree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    Block *blockToFree = (Block *)((char *)ptr - sizeof(Block));

    // coalesce with next block if possible
    Block *current = head;
    Block *prev = NULL;
    while (current != NULL) {
        if ((char *)blockToFree + sizeof(Block) + blockToFree->size == (char *)current) {
            // merge with next block
            blockToFree->size += sizeof(Block) + current->size;
            blockToFree->next = current->next;
            if (prev != NULL) {
                prev->next = blockToFree;
            } else {
                head = blockToFree;
            }
            return;
        }
        prev = current;
        current = current->next;
    }

    // add the block to the start of the free list
    blockToFree->next = head;
    head = blockToFree;
}


void *kurealloc(void *ptr, size_t size)
{

   if (ptr == NULL) {
        return kumalloc(size);
    }

    Block* block = (Block*)((char*)ptr - sizeof(Block));

    // if the new size is smaller split the block
    if (block->size > size) {
        size_t remainingSize = block->size - size;
        if (remainingSize > sizeof(Block)) {
            Block* newBlock = (Block*)((char*)block + sizeof(Block) + size);
            newBlock->size = remainingSize - sizeof(Block);
            newBlock->next = block->next;
            block->size = size;
            block->next = newBlock;
        }
        return ptr;
    }

    // if the new size is larger try to extend the block
    if (block->size < size) {
        // allocate a new block
        void* newPtr = kumalloc(size);
        if (newPtr != NULL) {
            // copy the contents from the old block to the new block
            memcpy(newPtr, ptr, block->size);
            // free the old block
            kufree(ptr);
            return newPtr;
        } else {
            return NULL; 
        }
    }

    return ptr;
}

/*
 * Enable the code below to enable system allocator support for your allocator.
 * Doing so will make debugging much harder (e.g., using printf may result in
 * infinite loops).
 */
#if 0
void *malloc(size_t size) { return kumalloc(size); }
void *calloc(size_t nmemb, size_t size) { return kucalloc(nmemb, size); }
void *realloc(void *ptr, size_t size) { return kurealloc(ptr, size); }
void free(void *ptr) { kufree(ptr); }
#endif