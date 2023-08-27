#include <stdint.h>

#include "memory_block.h"
#include "set_associative.h"

set_associative_cache* sac_init(main_memory* mm)
{
  set_associative_cache* result = malloc(sizeof(set_associative_cache));
  result->mm = mm;
  result->cs = cs_init();

  for (int s = 0; s < SET_ASSOCIATIVE_NUM_SETS; s++)
  {
    for (int w = 0; w < SET_ASSOCIATIVE_NUM_WAYS; w++)
    {
      result->valid[s][w] = 0;
      result->dirty[s][w] = 0;
      result->lru_tracker[s][w] = 0;
      result->tag[s][w] = MAIN_MEMORY_SIZE;
      result->memory_block_pointers[s][w] = NULL;
    }
  }

  return result;
}

static int addr_to_set(void* addr)
{
    int address = (uintptr_t) addr;
    int address_without_lowest_bits = address / MAIN_MEMORY_BLOCK_SIZE;
    int set_number_init = address_without_lowest_bits % (SET_ASSOCIATIVE_NUM_SETS);
    return set_number_init;
}

static void mark_as_used_when_hit(set_associative_cache* sac, int set, int way)
{
  for (int i = 0; i < SET_ASSOCIATIVE_NUM_WAYS; i++)
  {
    if (sac->lru_tracker[set][i] != 0 && i != way && sac->lru_tracker[set][i] < sac->lru_tracker[set][way])
    {
      sac->lru_tracker[set][i] = sac->lru_tracker[set][i] + 1;
    }
  }
  sac->lru_tracker[set][way] = 1;
}

static void mark_as_used(set_associative_cache* sac, int set, int way)
{
  // If this way has never been used before
  if (sac->lru_tracker[set][way] == 0)
  {
    for (int i = 0; i < SET_ASSOCIATIVE_NUM_WAYS; i++)
    {
      if (sac->lru_tracker[set][i] != 0)
      {
        sac->lru_tracker[set][i] = sac->lru_tracker[set][i] + 1;
      }
    }
    sac->lru_tracker[set][way] = 1;
  }
  // If the way does not have zero in it and has been used before
  else
  {
    for (int i = 0; i < SET_ASSOCIATIVE_NUM_WAYS; i++)
    {
      if (i != way)
      {
        sac->lru_tracker[set][i] = sac->lru_tracker[set][i] + 1;
      }
    }
    sac->lru_tracker[set][way] = 1;
  }
}

static int lru(set_associative_cache* sac, int set)
{
  int lru_count = 0;
  int index_of_lru = 0;
  for (int i = 0; i < SET_ASSOCIATIVE_NUM_WAYS; i++)
  {
    if (sac->lru_tracker[set][i] == 0)
    {
      return i;
    }
    else
    {
      if (sac->lru_tracker[set][i] > lru_count)
      {
        lru_count = sac->lru_tracker[set][i];
        index_of_lru = i;
      }
    }
  }

  return index_of_lru;
}


void sac_store_word(set_associative_cache* sac, void* addr, unsigned int val)
{
    // Check whether this address is in the cache.
    //int address = (uintptr_t) addr;
    size_t addr_offt = (size_t) (addr - MAIN_MEMORY_START_ADDR) % MAIN_MEMORY_BLOCK_SIZE;
    void* mb_start_addr = addr - addr_offt;

    int wayIndex = -1; // Set to the index if address is in the cache
    int set = addr_to_set(addr);
    for (int i = 0; i < SET_ASSOCIATIVE_NUM_WAYS; i++)
    {
      if ((uintptr_t) sac->tag[set][i] == (uintptr_t) mb_start_addr && sac->valid[set][i] == 1)
      {
        wayIndex = i;
      }
    }

    if (wayIndex > -1) // There was a hit
    {
      memory_block* address_memory_block = sac->memory_block_pointers[set][wayIndex];
      unsigned int* value = address_memory_block->data + addr_offt;
      *value = val;
      sac->dirty[set][wayIndex] = 1;
      ++sac->cs.w_queries;
      mark_as_used_when_hit(sac, set, wayIndex);
    }
    else // Miss
    {
      int wayNumber = lru(sac, set);

      //ADDED
      if (sac->dirty[set][wayNumber] == 1) // It's dirty and we have to write back the value to main memory
      {
        memory_block* replacement = sac->memory_block_pointers[set][wayNumber];
        mm_write(sac->mm, replacement->start_addr, replacement);
      }

      if (sac->memory_block_pointers[set][wayNumber])
      {
        mb_free(sac->memory_block_pointers[set][wayNumber]); // Free the memory block at the current wayNumber to replace it
      }
      // </ADDED>
      mark_as_used(sac, set, wayNumber);

      memory_block* mb = mm_read(sac->mm, mb_start_addr);
      sac->memory_block_pointers[set][wayNumber] = mb;
      unsigned int* value_pointer = mb->data + addr_offt;
      *value_pointer = val;

      sac->tag[set][wayNumber] = (uintptr_t) mb_start_addr;
      sac->valid[set][wayNumber] = 1;
      sac->dirty[set][wayNumber] = 1;

      ++sac->cs.w_queries;
      ++sac->cs.w_misses;

    }
}


unsigned int sac_load_word(set_associative_cache* sac, void* addr)
{
  // Check whether this address is in the cache.
  //int address = (uintptr_t) addr;
  size_t addr_offt = (size_t) (addr - MAIN_MEMORY_START_ADDR) % MAIN_MEMORY_BLOCK_SIZE;
  void* mb_start_addr = addr - addr_offt;

  int wayIndex = -1; // Set to the index if address is in the cache
  int set = addr_to_set(addr);
  for (int i = 0; i < SET_ASSOCIATIVE_NUM_WAYS; i++)
  {
    if ((uintptr_t) sac->tag[set][i] == (uintptr_t) mb_start_addr && sac->valid[set][i] == 1)
    {
      wayIndex = i;
    }
  }

  if (wayIndex > -1) // There was a hit
  {
    memory_block* address_memory_block = sac->memory_block_pointers[set][wayIndex];
    unsigned int* value = address_memory_block->data + addr_offt;
    ++sac->cs.r_queries;
    mark_as_used_when_hit(sac, set, wayIndex);
    return *value;
  }
  else // There was a miss and now we have to evict a piece of data in the corresponding set
  {
    int wayNumber = lru(sac, set);
    if (sac->dirty[set][wayNumber] == 1) // It's dirty and we have to write back the value to main memory
    {
      memory_block* replacement = sac->memory_block_pointers[set][wayNumber];
      mm_write(sac->mm, replacement->start_addr, replacement);
    }

    if (sac->memory_block_pointers[set][wayNumber])
    {
      mb_free(sac->memory_block_pointers[set][wayNumber]); // Free the memory block at the current wayNumber to replace it
    }

    mark_as_used(sac, set, wayNumber);

    // Now that we have dealt with the dirty data transfer we can replace the set with the right values
    sac->tag[set][wayNumber] = (uintptr_t) mb_start_addr;
    sac->valid[set][wayNumber] = 1;
    sac->dirty[set][wayNumber] = 0;

    memory_block* mb = mm_read(sac->mm, mb_start_addr);
    sac->memory_block_pointers[set][wayNumber] = mb;
    unsigned int* value_pointer = mb->data + addr_offt;
    unsigned int value = *value_pointer;

    ++sac->cs.r_queries;
    ++sac->cs.r_misses;

    return value;
  }

}

void sac_free(set_associative_cache* sac)
{
    for (int s = 0; s < SET_ASSOCIATIVE_NUM_SETS; s++) 
    {
        for (int w = 0; w < SET_ASSOCIATIVE_NUM_WAYS; w++)
        {
            if (sac->memory_block_pointers[s][w])
            {
                mb_free(sac->memory_block_pointers[s][w]);
            }
        }
    }
  
  free(sac);
}


