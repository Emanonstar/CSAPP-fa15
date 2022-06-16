#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "cachelab.h"

struct line 
{
    bool valid;
    unsigned long tag;
    unsigned int lastused;
};
typedef struct line line; 

unsigned s, E, b;
line* cache;
unsigned int hits, misses, evicts, time;

/* Return true if hit, false else. */
bool isHit(int S, unsigned long tag) 
{
    for (int i = S * E; i < S * E + E; i++) 
    {
        if (cache[i].valid && cache[i].tag == tag) 
        {
            cache[i].lastused = time;
            return true;
        }
    }
    return false;
}

/* Load data to cache. Return true if envicton happened, false else. */
bool load(int S, unsigned long tag) 
{
    unsigned int tmp = time;
    int index;
    for (int i = S * E; i < S * E + E; i++) 
    {
        if (!cache[i].valid) 
        {
            cache[i].valid = true;
            cache[i].tag = tag;
            cache[i].lastused = time;
            return false;
        }
        if (cache[i].lastused < tmp) 
        {
            tmp = cache[i].lastused;
            index = i;
        }
    }
    cache[index].valid = true;
    cache[index].tag = tag;
    cache[index].lastused = time;
    evicts += 1;
    return true;
}

int main(int argc, char *argv[]) 
{
    int opt;
    bool vflag = false;
    char *tracefile;

    while ((opt = getopt(argc, argv, "vs:E:b:t:")) != -1) 
    {
        switch (opt) {
        case 'v':
            vflag = true;
            break;
        case 's':
            s = atoi(optarg); 
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            tracefile = optarg;
            break;
        default:
            fprintf(stderr, "Usage: [-v] -s <set_bits> -E <lines_per_set> <-b block_bits> <-t tracefile>\n");
            exit(EXIT_FAILURE);
        }
    }

    if (s == 0 || E == 0 || b == 0 || tracefile == NULL) {
        printf("./csim: invalid input\nUsage: [-v] -s <set_bits> -E <lines_per_set> <-b block_bits> <-t tracefile>\n");
        exit(EXIT_FAILURE);
    }

    // allocate memory for cache
    int sets = (int) pow(2, s);
    cache = malloc(sizeof(line) * E * sets);

    // Open trace file
    FILE* fp = fopen(tracefile, "r");
    if (fp == NULL)
    {
        printf("Could not open %s.\n", tracefile);
        exit(EXIT_FAILURE);
    }

    // simulation
    char operation;
    unsigned long address;
    int size;
    while (fscanf(fp, " %c %lx,%d\n", &operation, &address, &size) != EOF)
    {
        // ignore instruction load
        if (operation == 'I') 
        {
            continue;
        }

        // parse address
        int S = address << (64 - s - b) >> (64 - s);
        unsigned long tag = address >> (s + b);
        bool hit = isHit(S, tag);
        switch (operation)
        {
        case 'L':
        case 'S':
            if (hit) 
            {
                hits += 1;
                if (vflag) 
                {
                    printf("%c %lx,%d hit\n", operation, address, size);
                }
            } else 
            {
                bool envict = load(S, tag);
                misses += 1;
                if (vflag) 
                {
                    printf("%c %lx,%d miss", operation, address, size);
                    printf(envict ? " enviction\n" : "\n");
                }
            }
            break;
        case 'M':
            if (hit) 
            {
                hits += 1;
                if (vflag) 
                {
                    printf("%c %lx,%d hit hit\n", operation, address, size);
                }
            } else 
            {
                bool envict = load(S, tag);
                misses += 1;
                if (vflag) 
                {
                    printf("%c %lx,%d miss ", operation, address, size);
                    printf(envict ? "enviction hit\n" : "hit\n");
                }
            }
            hits += 1;
            break;
        default:
            break;
        }
        time += 1;
    }
    printSummary(hits, misses, evicts);
    free(cache);
    fclose(fp);
    return 0;
}
