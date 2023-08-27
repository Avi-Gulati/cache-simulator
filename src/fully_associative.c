#include "memory_block.h"
#include "fully_associative.h"
#include <stdio.h>
#include <stdint.h>

fully_associative_cache* fac_init(main_memory* mm)
{
  fully_associative_cache* result = malloc(sizeof(fully_associative_cache));
  result->mm = mm;
  result->cs = cs_init();

  for (int i = 0; i < FULLY_ASSOCIATIVE_NUM_WAYS; i++)
  {
    result->valid[i] = 0;
    result->dirty[i] = 0;
    result->lru_tracker[i] = 0;
    result->tag[i] = MAIN_MEMORY_SIZE;
    result->memory_block_pointers[i] = NULL;
  }

  return result;
}

static void mark_as_used_when_hit(fully_associative_cache* fac, int way)
{
  for (int i = 0; i < FULLY_ASSOCIATIVE_NUM_WAYS; i++)
  {
    if (fac->lru_tracker[i] != 0 && i != way && fac->lru_tracker[i] < fac->lru_tracker[way])
    {
      fac->lru_tracker[i] = fac->lru_tracker[i] + 1;
    }
  }
  fac->lru_tracker[way] = 1;
}

// Only used when there has been a miss and something
static void mark_as_used(fully_associative_cache* fac, int way)
{
  // If this way has never been used before
  if (fac->lru_tracker[way] == 0)
  {
    for (int i = 0; i < FULLY_ASSOCIATIVE_NUM_WAYS; i++)
    {
      if (fac->lru_tracker[i] != 0)
      {
        fac->lru_tracker[i] = fac->lru_tracker[i] + 1;
      }
    }
    fac->lru_tracker[way] = 1;
  }
  // If the way does not have zero in it and has been used before
  else
  {
    for (int i = 0; i < FULLY_ASSOCIATIVE_NUM_WAYS; i++)
    {
      if (i != way)
      {
        fac->lru_tracker[i] = fac->lru_tracker[i] + 1;
      }
    }
    fac->lru_tracker[way] = 1;
  }
}


static int lru(fully_associative_cache* fac)
{
  int lru_count = 0;
  int index_of_lru = 0;
  for (int i = 0; i < FULLY_ASSOCIATIVE_NUM_WAYS; i++)
  {
    if (fac->lru_tracker[i] == 0)
    {
      return i;
    }
    else
    {
      if (fac->lru_tracker[i] > lru_count)
      {
        lru_count = fac->lru_tracker[i];
        index_of_lru = i;
      }
    }
  }

  return index_of_lru;
}


void fac_store_word(fully_associative_cache* fac, void* addr, unsigned int val)
{
    // Check whether this address is in the cache.
    size_t addr_offt = (size_t) (addr - MAIN_MEMORY_START_ADDR) % MAIN_MEMORY_BLOCK_SIZE;
    void* mb_start_addr = addr - addr_offt;

    int wayIndex = -1; // Set to the index if address is in the cache
    for (int i = 0; i < FULLY_ASSOCIATIVE_NUM_WAYS; i++)
    {
      if ((uintptr_t)fac->tag[i] == (uintptr_t)mb_start_addr && fac->valid[i] == 1)
      {
        wayIndex = i;
      }
    }

    if (wayIndex > -1) // There was a hit
    {
      memory_block* address_memory_block = fac->memory_block_pointers[wayIndex];
      unsigned int* value = address_memory_block->data + addr_offt;
      *value = val;
      fac->dirty[wayIndex] = 1;
      ++fac->cs.w_queries;
      mark_as_used_when_hit(fac, wayIndex);
    }
    else // Miss
    {
      int wayNumber = lru(fac);


      if (fac->dirty[wayNumber] == 1) // It's dirty and we have to write back the value to main memory
      {
        memory_block* replacement = fac->memory_block_pointers[wayNumber];
        mm_write(fac->mm, replacement->start_addr, replacement);
      }

      if (fac->memory_block_pointers[wayNumber])
      {
        mb_free(fac->memory_block_pointers[wayNumber]); // Free the memory block at the current wayNumber to replace it
      }

      mark_as_used(fac, wayNumber);

      memory_block* mb = mm_read(fac->mm, mb_start_addr);
      fac->memory_block_pointers[wayNumber] = mb;
      unsigned int* value_pointer = mb->data + addr_offt;
      *value_pointer = val;

      fac->tag[wayNumber] = (uintptr_t) mb_start_addr;
      fac->valid[wayNumber] = 1;
      fac->dirty[wayNumber] = 1;

      ++fac->cs.w_queries;
      ++fac->cs.w_misses;

    }
}


unsigned int fac_load_word(fully_associative_cache* fac, void* addr)
{
  // Check whether this address is in the cache.
  size_t addr_offt = (size_t) (addr - MAIN_MEMORY_START_ADDR) % MAIN_MEMORY_BLOCK_SIZE;
  void* mb_start_addr = addr - addr_offt;

  int wayIndex = -1; // Set to the index if address is in the cache
  for (int i = 0; i < FULLY_ASSOCIATIVE_NUM_WAYS; i++)
  {
    if ((uintptr_t)fac->tag[i] == (uintptr_t)mb_start_addr && fac->valid[i] == 1)
    {
      wayIndex = i;
    }
  }

  if (wayIndex > -1) // There was a hit
  {
    memory_block* address_memory_block = fac->memory_block_pointers[wayIndex];
    unsigned int* value = address_memory_block->data + addr_offt;
    ++fac->cs.r_queries;
    mark_as_used_when_hit(fac, wayIndex);
    return *value;
  }
  else // There was a miss and now we have to evict a piece of data in the corresponding set
  {
    int wayNumber = lru(fac);
    if (fac->dirty[wayNumber] == 1) // It's dirty and we have to write back the value to main memory
    {
      memory_block* replacement = fac->memory_block_pointers[wayNumber];
      mm_write(fac->mm, replacement->start_addr, replacement);
    }

    if (fac->memory_block_pointers[wayNumber])
    {
      mb_free(fac->memory_block_pointers[wayNumber]); // Free the memory block at the current wayNumber to replace it
    }

    mark_as_used(fac, wayNumber);

    // Now that we have dealt with the dirty data transfer we can replace the set with the right values
    fac->tag[wayNumber] = (uintptr_t)mb_start_addr;
    fac->valid[wayNumber] = 1;
    fac->dirty[wayNumber] = 0;

    memory_block* mb = mm_read(fac->mm, mb_start_addr);
    fac->memory_block_pointers[wayNumber] = mb;
    unsigned int* value_pointer = mb->data + addr_offt;
    unsigned int value = *value_pointer;

    ++fac->cs.r_queries;
    ++fac->cs.r_misses;

    return value;
  }

}


void fac_free(fully_associative_cache* fac)
{
  for (int i = 0; i < FULLY_ASSOCIATIVE_NUM_WAYS; i++)
  {
    if (fac->memory_block_pointers[i])
    {
      mb_free(fac->memory_block_pointers[i]);
    }
  }
  free(fac);
}


