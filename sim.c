#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "sim.h"
#include <stdbool.h>
#include <math.h>
#include <assert.h>
/*  "argc" holds the number of command-line arguments.
    "argv[]" holds the arguments themselves.


    Example:
    ./sim 32 8192 4 262144 8 3 10 gcc_trace.txt
    argc = 9
    argv[0] = "./sim"
    argv[1] = "32"
    argv[2] = "8192"
    ... and so on
*/

// REQUIRED FUNCTIONS
/*static bool two_power_check(uint32_t assoc){
	uint32_t i;
	uint32_t c = 0;
	uint32_t n= assoc;
	for(i=0;i<sizeof(uint32_t)<<3;i++){
		if(n & 1) c++;
		n=n>>1;
	}
	return (c==1);}
*/

size_t g_num_memory_read = 0;
size_t g_num_memory_write = 0;

typedef struct cache_stat {
	size_t num_r;        // Number of reads
	size_t num_w;        // Number of writes
	size_t num_r_miss;    // Number of read misses
	size_t num_w_miss;    // Number of write misses
	size_t num_w_back;    // Number of write backs issued.
} cache_stat_t;        // This is where the statistics part of cache sim comes.

struct cache_property;

typedef void (*mem_access_fn_t)(struct cache_property *property, uint32_t addr, bool is_write, bool is_prefetch);

typedef struct cache_property {
	size_t num_set;
	size_t num_way;
	size_t blk_size;
	size_t tag_bits;
	size_t index_bits;
	size_t blkoffset_bits;
	block_t **block_storage;
	struct cache_property *next_level_property;
	mem_access_fn_t access_next_level;
	cache_stat_t cache_stat;
} cache_property_t;

/*
 * |       ADDRESS            |
 * |  BLOCK ADDR | 0          |
 * | TAG | INDEX | BLK OFFSET |
 */
// Calculates the block address of a cache block from its index and tag
#define calc_addr(tag, index) ((tag << (this->index_bits + this->blkoffset_bits)) | ( index << this->blkoffset_bits ))
#define calc_index(addr) (((1<<this->index_bits)-1) & ((addr) >> this->blkoffset_bits)) // Calculates the index portion of the given address
#define calc_tag(addr) ((addr)  >> ((this->index_bits+this->blkoffset_bits))) // Calculates the Tag portion of the given address

size_t find_lru_way(cache_property_t *this, uint32_t index) {
	size_t i;
	for (i = 0; i < this->num_way; i++) {
		if (this->block_storage[index][i].LRU == this->num_way - 1) {
			break;
		}
	}
	assert(i < this->num_way);
	return i;
}

void evict(cache_property_t *this, uint32_t index, size_t victim_way) {
	/*
	 * |       ADDRESS            |
	 * |  BLOCK ADDR |  0         |
	 * | TAG | INDEX | BLK OFFSET |
	 */
	if (this->block_storage[index][victim_way].VB && this->block_storage[index][victim_way].DB) {
		this->cache_stat.num_w_back++;
		this->access_next_level(this->next_level_property, calc_addr(
				                        this->block_storage[index][victim_way].TAG, index
		                        ), true, false
		);
	}
	this->block_storage[index][victim_way].VB = false;
}

void fill_block(cache_property_t *this, uint32_t index, size_t install_way, uint32_t req_tag) {
	this->access_next_level(this->next_level_property, calc_addr(req_tag, index), false, false);
	this->block_storage[index][install_way].TAG = req_tag;
	this->block_storage[index][install_way].VB = true;
	this->block_storage[index][install_way].DB = false;
}

size_t search_cache(cache_property_t *this, uint32_t index, uint32_t tag) {
	size_t i;
	for (i = 0; i < this->num_way; i++) {
		if (this->block_storage[index][i].VB && this->block_storage[index][i].TAG == tag)
			break;
	}
	return i;
}

void lru_update(cache_property_t *this, uint32_t index, size_t way) {
	for (size_t i = 0; i < this->num_way; i++) {
		if (this->block_storage[index][i].LRU < this->block_storage[index][way].LRU)
			this->block_storage[index][i].LRU++;
	}
	this->block_storage[index][way].LRU = 0;
}

void cache_access(cache_property_t *this, uint32_t addr, bool is_write, bool is_prefetch) {
	if (is_write) {
		assert(!is_prefetch);
		this->cache_stat.num_w++;
	} else {
		this->cache_stat.num_r++;
	}

	uint32_t tag = calc_tag(addr);
	uint32_t index = calc_index(addr);
	/*
	 * Step 1. calc the index and tag from address
	 * Step 2. size_t search_cache(index, tag) -> search the cache for the requested memory block
	 * Step hit.1 update LRU of the hit cache block,
	 * Step hit.2 set dirty bit if it is a write hit
	 * Step miss.1 find_lru_way() -> pick the LRU way in that set, that way becomes the victim
	 * Step.miss.2 evict() -> evict the victim block from the cache
	 *                  check whether the victim block is a valid and dirty block
	 *                         if yes, issue memory writeback to next level
	 * Step.miss.3 fill_block() -> load the requested memory block from next level, set tag, valid = true, dirty = false
	 * Step.miss.4 update the LRU of the newly filled block as the MRU
	 * Step.miss.5 set dirty bit if it is a write miss
	 */
	size_t hit_way = search_cache(this, index, tag);
	bool hit = hit_way < this->num_way;

	if (hit) {
		// access hit
		lru_update(this, index, hit_way);
		if (is_write) this->block_storage[index][hit_way].DB = true;
	} else {
		// access miss
		if (is_write) {
			assert(!is_prefetch);
			this->cache_stat.num_w_miss++;
		} else {
			this->cache_stat.num_r_miss++;
		}
		size_t victim_way = find_lru_way(this, index);
		evict(this, index, victim_way);
		fill_block(this, index, victim_way, tag);
		lru_update(this, index, victim_way);
		if (is_write) this->block_storage[index][victim_way].DB = true;
	}
}

void main_memory_access(struct cache_property *property, uint32_t addr, bool is_write, bool is_prefetch) {
	if (is_write == true) {
		g_num_memory_write++;
	} else {
		g_num_memory_read++;
	}
}

void initialize_cache_property(cache_property_t *this, uint32_t num_set, size_t num_way,
                               uint32_t blk_size) {
	this->cache_stat.num_r = 0;
	this->cache_stat.num_w = 0;
	this->cache_stat.num_r_miss = 0;
	this->cache_stat.num_w_miss = 0;
	this->cache_stat.num_w_back = 0;
	if (num_set == 0) return;

	this->num_set = num_set;
	this->num_way = num_way;
	this->blk_size = blk_size;
	this->index_bits = (size_t) log2(num_set);
	this->blkoffset_bits = (size_t) log2(blk_size);
	this->tag_bits = 32 - this->index_bits - this->blkoffset_bits;

	this->block_storage = (block_t **) malloc(
			num_set * (sizeof(block_t *)));    //Allocating dynamic memory to the block.

	for (uint32_t i = 0; i < num_set; i++) {
		this->block_storage[i] = (block_t *) malloc(num_way * sizeof(block_t));
		for (size_t j = 0; j < num_way; j++) {
			this->block_storage[i][j].VB = false;
			this->block_storage[i][j].DB = false;
			this->block_storage[i][j].TAG = 0;
			this->block_storage[i][j].LRU = j;
		}
	}
}

void print_cache_content(cache_property_t *this, const char *cache_level_name) {
	printf("===== %s contents =====\n", cache_level_name);
	for (size_t curr_set = 0; curr_set < this->num_set; ++curr_set) {
		printf("set %zu:", curr_set);
		for (size_t curr_lru = 0; curr_lru < this->num_way; ++curr_lru) {
			for (size_t i = 0; i < this->num_way; ++i) {
				if (this->block_storage[curr_set][i].LRU == curr_lru) {
					printf(" %x %s",
					       this->block_storage[curr_set][i].TAG,
					       this->block_storage[curr_set][i].DB ? "D" : " "
					);
				}
			}
		}
		printf("\n");
	}
	printf("\n");
}


int main(int argc, char *argv[]) {
	FILE *fp;            // File pointer.
	char *trace_file;        // This variable holds the trace file name.
	cache_params_t params;    // Look at the sim.h header file for the definition of struct cache_params_t.
	char rw;            // This variable holds the request's type (read or write) obtained from the trace.
	uint32_t addr;        // This variable holds the request's address obtained from the trace.
	// The header file <inttypes.h> above defines signed and unsigned integers of various sizes in a machine-agnostic way.  "uint32_t" is an unsigned integer of 32 bits.
	// Exit with an error if the number of command-line arguments is incorrect.

	cache_property_t l1_cache_property;
	cache_property_t l2_cache_property;


	if (argc != 9) {
		printf("Error: Expected 8 command-line arguments but was provided %d.\n", (argc - 1));
		exit(EXIT_FAILURE);
	}

	// "atoi()" (included by <stdlib.h>) converts a string (char *) to an integer (int).
	params.BLOCKSIZE = (uint32_t) atoi(argv[1]);// Changed 31 to 32 in uint32_t
	params.L1_SIZE = (uint32_t) atoi(argv[2]);
	params.L1_ASSOC = (uint32_t) atoi(argv[3]);
	params.L2_SIZE = (uint32_t) atoi(argv[4]);
	params.L2_ASSOC = (uint32_t) atoi(argv[5]);
	params.PREF_N = (uint32_t) atoi(argv[6]);
	params.PREF_M = (uint32_t) atoi(argv[7]);
	trace_file = argv[8];

	if (params.L2_SIZE == 0) {
		// this is L1 only simulation
		uint32_t l1_num_set = (params.L1_SIZE) / (params.L1_ASSOC * params.BLOCKSIZE);
		initialize_cache_property(&l1_cache_property, l1_num_set, params.L1_ASSOC, params.BLOCKSIZE);
		initialize_cache_property(&l2_cache_property, 0, 0, 0);
		l1_cache_property.access_next_level = main_memory_access;
		l1_cache_property.next_level_property = NULL;
	} else {
		// this is L1+L2 simulation
		uint32_t l1_num_set = (params.L1_SIZE) / (params.L1_ASSOC * params.BLOCKSIZE);
		uint32_t l2_num_set = (params.L2_SIZE) / (params.L2_ASSOC * params.BLOCKSIZE);
		initialize_cache_property(&l1_cache_property, l1_num_set, params.L1_ASSOC, params.BLOCKSIZE);
		initialize_cache_property(&l2_cache_property, l2_num_set, params.L2_ASSOC, params.BLOCKSIZE);
		l1_cache_property.access_next_level = cache_access;
		l1_cache_property.next_level_property = &l2_cache_property;
		l2_cache_property.access_next_level = main_memory_access;
		l2_cache_property.next_level_property = NULL;
	}

	// Open the trace file for reading.
	fp = fopen(trace_file, "r");
	if (fp == (FILE *) NULL) {
		// Exit with an error if file open failed.
		printf("Error: Unable to open file %s\n", trace_file);
		exit(EXIT_FAILURE);
	}

	// Print simulator configuration.
	printf("===== Simulator configuration =====\n");
	printf("BLOCKSIZE:  %u\n", params.BLOCKSIZE);
	printf("L1_SIZE:    %u\n", params.L1_SIZE);
	printf("L1_ASSOC:   %u\n", params.L1_ASSOC);
	printf("L2_SIZE:    %u\n", params.L2_SIZE);
	printf("L2_ASSOC:   %u\n", params.L2_ASSOC);
	printf("PREF_N:     %u\n", params.PREF_N);
	printf("PREF_M:     %u\n", params.PREF_M);
	printf("trace_file: %s\n\n", trace_file);


	// Read requests from the trace file and echo them back.
	while (fscanf(fp, "%c %x\n", &rw, &addr) == 2) {
		// Stay in the loop if fscanf() successfully parsed two tokens as specified.
		if (rw == 'r') {
			cache_access(&l1_cache_property, addr, false, false);
		} else if (rw == 'w') {
			cache_access(&l1_cache_property, addr, true, false);
		} else {
			printf("Error: Unknown request type %c.\n", rw);
			exit(EXIT_FAILURE);
		}

	}

	if (params.L2_SIZE == 0) {
		print_cache_content(&l1_cache_property, "L1");
	} else {
		print_cache_content(&l1_cache_property, "L1");
		print_cache_content(&l2_cache_property, "L2");
	}

	float l1_miss_rate = ((float) (l1_cache_property.cache_stat.num_r_miss + l1_cache_property.cache_stat.num_w_miss)) /
	                     ((float) (l1_cache_property.cache_stat.num_r + l1_cache_property.cache_stat.num_w));
	float l2_miss_rate = 0;
	if (params.L2_SIZE > 0) {
		l2_miss_rate =
				((float) (l2_cache_property.cache_stat.num_r_miss)) / ((float) (l2_cache_property.cache_stat.num_r));
	}
	printf("===== Measurements =====\n");
	printf("a. L1 reads:                   %zu\n", l1_cache_property.cache_stat.num_r);
	printf("b. L1 read misses:             %zu\n", l1_cache_property.cache_stat.num_r_miss);
	printf("c. L1 writes:                  %zu\n", l1_cache_property.cache_stat.num_w);
	printf("d. L1 write misses:            %zu\n", l1_cache_property.cache_stat.num_w_miss);
	printf("e. L1 miss rate:               %0.4f\n", l1_miss_rate);
	printf("f. L1 writebacks:              %zu\n", l1_cache_property.cache_stat.num_w_back);
	printf("g. L1 prefetches:              %zu\n", 0ul);
	printf("h. L2 reads (demand):          %zu\n", l2_cache_property.cache_stat.num_r);
	printf("i. L2 read misses (demand):    %zu\n", l2_cache_property.cache_stat.num_r_miss);
	printf("j. L2 reads (prefetch):        %zu\n", 0ul);
	printf("k. L2 read misses (prefetch):  %zu\n", 0ul);
	printf("l. L2 writes:                  %zu\n", l2_cache_property.cache_stat.num_w);
	printf("m. L2 write misses:            %zu\n", l2_cache_property.cache_stat.num_w_miss);
	printf("n. L2 miss rate:               %0.4f\n", l2_miss_rate);
	printf("o. L2 writebacks:              %zu\n", l2_cache_property.cache_stat.num_w_back);
	printf("p. L2 prefetches:              %zu\n", 0ul);
	printf("q. memory traffic:             %zu\n", g_num_memory_read + g_num_memory_write);

	return (0);
}
