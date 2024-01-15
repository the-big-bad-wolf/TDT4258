#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
  dm,
  fa
} cache_map_t;
typedef enum
{
  uc,
  sc
} cache_org_t;
typedef enum
{
  instruction,
  data
} access_t;

typedef struct
{
  uint32_t address;
  access_t accesstype;
} mem_access_t;

typedef struct
{
  uint64_t accesses;
  uint64_t hits;
  // You can declare additional statistics if
  // you like, however you are now allowed to
  // remove the accesses or hits
} cache_stat_t;

// DECLARE CACHES AND COUNTERS FOR THE STATS HERE

uint32_t cache_size;
uint32_t block_size = 64;
cache_map_t cache_mapping;
cache_org_t cache_org;

typedef struct
{
  int valid;
  uint32_t tag;
} block;

typedef struct cache
{
  block *blocks;
} cache;

uint32_t nr_of_blocks;

typedef struct block_queue
{
  int *block_indexes;
  int head;
  int tail;
} block_queue;

void fully_associative(cache data_cache, block_queue *data_queue, cache instruction_cache, block_queue *instruction_queue, mem_access_t access);
void direct_mapped(cache data_cache, cache instruction_cache, mem_access_t access);

// USE THIS FOR YOUR CACHE STATISTICS
cache_stat_t cache_statistics;

/* Reads a memory access from the trace file and returns
 * 1) access type (instruction or data access
 * 2) memory address
 */
mem_access_t read_transaction(FILE *ptr_file)
{
  char type;
  mem_access_t access;

  if (fscanf(ptr_file, "%c %x\n", &type, &access.address) == 2)
  {
    if (type != 'I' && type != 'D')
    {
      printf("Unkown access type\n");
      exit(0);
    }
    access.accesstype = (type == 'I') ? instruction : data;
    return access;
  }

  /* If there are no more entries in the file,
   * return an address 0 that will terminate the infinite loop in main
   */
  access.address = 0;
  return access;
}

void main(int argc, char **argv)
{
  // Reset statistics:
  memset(&cache_statistics, 0, sizeof(cache_stat_t));

  /* Read command-line parameters and initialize:
   * cache_size, cache_mapping and cache_org variables
   */
  /* IMPORTANT: *IF* YOU ADD COMMAND LINE PARAMETERS (you really don't need to),
   * MAKE SURE TO ADD THEM IN THE END AND CHOOSE SENSIBLE DEFAULTS SUCH THAT WE
   * CAN RUN THE RESULTING BINARY WITHOUT HAVING TO SUPPLY MORE PARAMETERS THAN
   * SPECIFIED IN THE UNMODIFIED FILE (cache_size, cache_mapping and cache_org)
   */
  if (argc != 4)
  { /* argc should be 2 for correct execution */
    printf(
        "Usage: ./cache_sim [cache size: 128-4096] [cache mapping: dm|fa] "
        "[cache organization: uc|sc]\n");
    exit(0);
  }
  else
  {
    /* argv[0] is program name, parameters start with argv[1] */

    /* Set cache size */
    cache_size = atoi(argv[1]);

    /* Set Cache Mapping */
    if (strcmp(argv[2], "dm") == 0)
    {
      cache_mapping = dm;
    }
    else if (strcmp(argv[2], "fa") == 0)
    {
      cache_mapping = fa;
    }
    else
    {
      printf("Unknown cache mapping\n");
      exit(0);
    }

    /* Set Cache Organization */
    if (strcmp(argv[3], "uc") == 0)
    {
      cache_org = uc;
    }
    else if (strcmp(argv[3], "sc") == 0)
    {
      cache_org = sc;
    }
    else
    {
      printf("Unknown cache organization\n");
      exit(0);
    }
  }

  /* Open the file mem_trace.txt to read memory accesses */
  FILE *ptr_file;
  ptr_file = fopen("mem_trace.txt", "r");
  if (!ptr_file)
  {
    printf("Unable to open the trace file\n");
    exit(1);
  }

  nr_of_blocks = cache_size / block_size;
  /*Divide number of blocks in each cache in two if seperate data and instruction cache*/
  if (cache_org == sc)
  {
    nr_of_blocks = nr_of_blocks / 2;
  }

  cache data_cache; // I use data cache for both data and instructions if UC
  cache instruction_cache;
  block_queue queue_data; // Data structure that ensures FIFO eviction policy. I use the data queue for data and instructions in case of UC
  block_queue queue_instructions;
  data_cache.blocks = calloc(nr_of_blocks, sizeof(block));
  instruction_cache.blocks = calloc(nr_of_blocks, sizeof(block));

  queue_data.block_indexes = malloc(sizeof(int) * nr_of_blocks);
  queue_data.head = -1;
  queue_data.tail = -1;
  queue_instructions.block_indexes = malloc(sizeof(int) * nr_of_blocks);
  queue_instructions.head = -1;
  queue_instructions.tail = -1;

  /* Loop until whole trace file has been read */
  mem_access_t access;
  while (1)
  {
    access = read_transaction(ptr_file);
    // If no transactions left, break out of loop
    if (access.address == 0)
      break;
    printf("%d %x\n", access.accesstype, access.address);
    /* Do a cache access */
    // ADD YOUR CODE HERE
    cache_statistics.accesses += 1;

    /* I'm using the same function for universal and separate caches, but in the case of universal caches I use the data cache for both data and instructions */
    if (cache_mapping == fa && cache_org == uc)
    {
      fully_associative(data_cache, &queue_data, data_cache, &queue_data, access);
    }
    else if (cache_mapping == fa && cache_org == sc)
    {
      fully_associative(data_cache, &queue_data, instruction_cache, &queue_instructions, access);
    }
    else if (cache_mapping == dm && cache_org == uc)
    {
      direct_mapped(data_cache, data_cache, access);
    }
    else if (cache_mapping == dm && cache_org == sc)
    {
      direct_mapped(data_cache, instruction_cache, access);
    }
  }

  /* Print the statistics */
  // DO NOT CHANGE THE FOLLOWING LINES!
  printf("\nCache Statistics\n");
  printf("-----------------\n\n");
  printf("Accesses: %ld\n", cache_statistics.accesses);
  printf("Hits:     %ld\n", cache_statistics.hits);
  printf("Hit Rate: %.4f\n",
         (double)cache_statistics.hits / cache_statistics.accesses);
  // DO NOT CHANGE UNTIL HERE
  // You can extend the memory statistic printing if you like!

  /* Close the trace file */
  fclose(ptr_file);
}

void direct_mapped(cache data_cache, cache instruction_cache, mem_access_t access)
{
  cache cache;
  if (access.accesstype == instruction)
  {
    cache = instruction_cache;
  }
  else if (access.accesstype == data)
  {
    cache = data_cache;
  }
  uint32_t address = access.address >> 6;   // Right-shift address 6 bits to get rid of offset bits
  uint32_t address_mask = nr_of_blocks - 1; // Create mask that is equal to 1 for all the index bits
  uint32_t index = address & address_mask;
  uint32_t tag = address & ~address_mask; // Invert the index mask and use it to get the remaining bits which are the tag bits

  if (!cache.blocks[index].valid) // cache-miss if block invalid
  {
    cache.blocks[index].tag = tag;
    cache.blocks[index].valid = 1;
    return;
  }
  if (cache.blocks[index].tag == tag)
  {
    cache_statistics.hits += 1;
    return;
  }
  else
  {
    cache.blocks[index].tag = tag; // Simply replace and return if cache-miss
    return;
  }
}

void enqueue(block_queue *queue, int block_index)
{
  /*Initialization*/
  if (queue->head == -1)
  {
    queue->head = 0;
  }
  /*Moves tail to beginning of array ring-buffer style if tail is at the end of the array*/
  if (queue->tail == nr_of_blocks - 1)
  {
    queue->tail = 0;
    queue->block_indexes[queue->tail] = block_index; // Insert the index of the latest cache block to be replaced
  }
  else
  {
    queue->tail++;
    queue->block_indexes[queue->tail] = block_index;
  }
}

int dequeue(block_queue *queue)
{
  int block_index = queue->block_indexes[queue->head];
  if (queue->head == nr_of_blocks - 1)
  {
    queue->head = 0;
  }
  else
  {
    queue->head++;
  }

  return block_index; // Returns the index of the oldest cache block
}

void fully_associative(cache data_cache, block_queue *data_queue, cache instruction_cache, block_queue *instruction_queue, mem_access_t access)
{
  int last_invalid = -1;
  cache cache;
  block_queue *queue;
  if (access.accesstype == instruction)
  {
    cache = instruction_cache;
    queue = instruction_queue;
  }
  else if (access.accesstype == data)
  {
    cache = data_cache;
    queue = data_queue;
  }

  for (size_t i = 0; i < nr_of_blocks; i++) // Scan through cache
  {
    if (cache.blocks[i].valid)
    {
      uint32_t address_tag = access.address >> 6;
      if (cache.blocks[i].tag == address_tag)
      {
        cache_statistics.hits += 1;
        return;
      }
    }
    else
    {
      last_invalid = i;
    }
  }
  if (last_invalid > -1)
  {
    int32_t address_tag = access.address >> 6;
    cache.blocks[last_invalid].tag = address_tag;
    cache.blocks[last_invalid].valid = 1;
    enqueue(queue, last_invalid);
    return;
  }
  else
  {
    int replace_index = dequeue(queue);
    int32_t address_tag = access.address >> 6;
    cache.blocks[replace_index].tag = address_tag;
    enqueue(queue, replace_index);
    return;
  }
}