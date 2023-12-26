#include <stdbool.h>
#include <stdlib.h>
#ifndef SIM_CACHE_H
#define SIM_CACHE_H

typedef 
struct {
   uint32_t BLOCKSIZE;
   uint32_t L1_SIZE;
   uint32_t L1_ASSOC;
   uint32_t L2_SIZE;
   uint32_t L2_ASSOC;
   uint32_t PREF_N;
   uint32_t PREF_M;
} cache_params_t;
#endif
// Put additional data structures here as per your requirement.
typedef struct block
{
	bool DB; // Dirty bit.
	bool VB; // Valid bit.
	uint32_t TAG; // Tag address.
	size_t LRU;
} block_t;

typedef struct add_info
{
	uint32_t TOTAL_WIDTH;
	uint32_t TAG_WID;
	uint32_t IND_WID;
	uint32_t OFF_WID;
}add_info_t;

typedef struct cache
{
	uint32_t SZ; //Cache Size.
	uint32_t AS; //Associative.
	uint32_t NS; //No. of sets.
	uint32_t BS; //Block Size.
	struct block BK;
	block_t BK_alias;
} cache_t;
//#endif
