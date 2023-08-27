#include <stdint.h>
#include <stdio.h>
#include "memory_block.h"
#include "direct_mapped.h"

direct_mapped_cache* dmc_init(main_memory* mm)
{
    direct_mapped_cache* result = malloc(sizeof(direct_mapped_cache));
    result->mm = mm;
    result->cs = cs_init();

    for (int i = 0; i < DIRECT_MAPPED_NUM_SETS; i++)
    {
      result->valid[i] = 0;
      result->dirty[i] = 0;
      result->tag[i] = MAIN_MEMORY_SIZE;
      result->memory_block_pointers[i] = NULL;
    }
    return result;
}


static int addr_to_set(void* addr)
{
    int address = (uintptr_t) addr;
    int address_without_lowest_bits = address / MAIN_MEMORY_BLOCK_SIZE;
    int set_number_init = address_without_lowest_bits % (DIRECT_MAPPED_NUM_SETS);
    return set_number_init;
}


void dmc_store_word(direct_mapped_cache* dmc, void* addr, unsigned int val)
{
    // Check whether this address is in the cache.
    size_t addr_offt = (size_t) (addr - MAIN_MEMORY_START_ADDR) % MAIN_MEMORY_BLOCK_SIZE;
    void* mb_start_addr = addr - addr_offt;

    int cacheIndex = -1; // Set to the index if address is in the cache
    for (int i = 0; i < DIRECT_MAPPED_NUM_SETS; i++)
    {
      if ((uintptr_t) dmc->tag[i] == (uintptr_t) mb_start_addr && dmc->valid[i] == 1)
      {
        cacheIndex = i;
      }
    }

    if (cacheIndex > -1) // There was a hit
    {
      memory_block* address_memory_block = dmc->memory_block_pointers[cacheIndex];
      unsigned int* value = address_memory_block->data + addr_offt;
      *value = val;
      dmc->dirty[cacheIndex] = 1;
      ++dmc->cs.w_queries;
    }
    else // Miss
    {
      int setNumber = addr_to_set(addr);

      //ADDED
      if (dmc->dirty[setNumber] == 1) // It's dirty and we have to write back the value to main memory
      {
        memory_block* replacement = dmc->memory_block_pointers[setNumber];
        mm_write(dmc->mm, replacement->start_addr, replacement);
      }

      if (dmc->memory_block_pointers[setNumber])
      {
        mb_free(dmc->memory_block_pointers[setNumber]); // Free the memory block at the current setNumber to replace it
      }
      // </ADDED>

      memory_block* mb = mm_read(dmc->mm, mb_start_addr);
      dmc->memory_block_pointers[setNumber] = mb;
      unsigned int* value_pointer = mb->data + addr_offt;
      *value_pointer = val;

      dmc->tag[setNumber] = (uintptr_t) mb_start_addr;
      dmc->valid[setNumber] = 1;
      dmc->dirty[setNumber] = 1;

      ++dmc->cs.w_queries;
      ++dmc->cs.w_misses;

    }
}

unsigned int dmc_load_word(direct_mapped_cache* dmc, void* addr)
{
  // Check whether this address is in the cache.
  size_t addr_offt = (size_t) (addr - MAIN_MEMORY_START_ADDR) % MAIN_MEMORY_BLOCK_SIZE;
  void* mb_start_addr = addr - addr_offt;

  int cacheIndex = -1; // Set to the index if address is in the cache
  for (int i = 0; i < DIRECT_MAPPED_NUM_SETS; i++)
  {
    if ((uintptr_t) dmc->tag[i] == (uintptr_t) mb_start_addr && dmc->valid[i] == 1)
    {
      cacheIndex = i;
    }
  }

  if (cacheIndex > -1) // There was a hit
  {
    memory_block* address_memory_block = dmc->memory_block_pointers[cacheIndex];
    unsigned int* value = address_memory_block->data + addr_offt;
    ++dmc->cs.r_queries;
    return *value;
  }
  else // There was a miss and now we have to evict a piece of data in the corresponding set
  {
    int setNumber = addr_to_set(addr);
    if (dmc->dirty[setNumber] == 1) // It's dirty and we have to write back the value to main memory
    {
      memory_block* replacement = dmc->memory_block_pointers[setNumber];
      mm_write(dmc->mm, replacement->start_addr, replacement);
    }

    if (dmc->memory_block_pointers[setNumber])
    {
      mb_free(dmc->memory_block_pointers[setNumber]); // Free the memory block at the current setNumber to replace it
    }

    // Now that we have dealt with the dirty data transfer we can replace the set with the right values
    dmc->tag[setNumber] = (uintptr_t) mb_start_addr;
    dmc->valid[setNumber] = 1;
    dmc->dirty[setNumber] = 0;

    memory_block* mb = mm_read(dmc->mm, mb_start_addr);
    dmc->memory_block_pointers[setNumber] = mb;
    unsigned int* value_pointer = mb->data + addr_offt;
    unsigned int value = *value_pointer;

    ++dmc->cs.r_queries;
    ++dmc->cs.r_misses;

    return value;
  }

}

void dmc_free(direct_mapped_cache* dmc)
{
  for (int i = 0; i < DIRECT_MAPPED_NUM_SETS; i++)
  {
    if (dmc->memory_block_pointers[i])
    {
      mb_free(dmc->memory_block_pointers[i]);
    }
  }
  free(dmc);
}
