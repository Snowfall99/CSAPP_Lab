#include "cachelab.h"
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

int h, v, s, E, b, S;   // parameters
char t[1000];           // trace name
int hit_count, miss_count, eviction_count; // hit count, miss count, evict count

typedef struct {
    int valid_bit;  // valid bit
    int tag;        // tag
    int stamp;      // time stamp
}cache_line, *cache_asso, **cache;

cache _cache_ = NULL;

// init cache
void init_cache() {
    _cache_ = (cache)malloc(sizeof(cache_asso) * S);
    for (int i=0; i<S; i++) {
        _cache_[i] = (cache_asso)malloc(sizeof(cache_line) * E);
        for (int j=0; j<E; j++) {
            _cache_[i][j].valid_bit = 0;
            _cache_[i][j].tag = -1;
            _cache_[i][j].stamp = -1;
        }
    }
}

// free cache
void free_cache() {
    for (int i=0; i<S; i++) {
        free(_cache_[i]);
    }
    free(_cache_);
}

void update(unsigned int address) {
    int index = (address >> b) & ((-1U) >> (64-s));
    int tag = (address >> (b+s));

    // check if hit
    for (int i=0; i<E; i++) {
        if (_cache_[index][i].tag == tag) {
            _cache_[index][i].stamp = 0;
            hit_count ++;
            return;
        }
    }
    // check empty line
    for (int i=0; i<E; i++) {
        if (_cache_[index][i].valid_bit == 0) {
            _cache_[index][i].valid_bit = 1;
            _cache_[index][i].tag = tag;
            _cache_[index][i].stamp = 0;
            miss_count ++;
            return;
        }
    }
    // evict line
    miss_count ++;
    eviction_count ++;
    int max_stamp = INT_MIN;
    int max_stamp_index = -1;
    for (int i=0; i<E; i++) {
        if (_cache_[index][i].stamp > max_stamp) {
            max_stamp = _cache_[index][i].stamp;
            max_stamp_index = i;
        }
    }
    _cache_[index][max_stamp_index].stamp = 0;
    _cache_[index][max_stamp_index].tag = tag;
    return;
}

void update_stamp() {
    for (int i=0; i<S; i++) {
        for (int j=0; j<E; j++) {
            if (_cache_[i][j].valid_bit == 1) {
                _cache_[i][j].stamp ++;
            }
        }
    }
}

// parse
void parse(FILE *fp) {
    char operation;
    unsigned int address;
    int size;
    
    while (fscanf(fp, "%c %xu %d\n", &operation, &address, &size) > 0) {
        switch (operation) {
            case 'I':
                continue;
            case 'L':
                update(address);
                break;
            case 'M':
                update(address);
            case 'S':
                update(address);
            default:
                printf("Wrong operation.\n");
                break;
        }
        update_stamp();
    }
}

void printHelper() {
    printf("Usage: ./csim-ref [-hv] -s <num> -E <num> -b <num> -t <file>\n"
            "Options:\n"
            "  -h         Print this help message.\n"
            "  -v         Optional verbose flag.\n"
            "  -s <num>   Number of set index bits.\n"
            "  -E <num>   Number of lines per set.\n"
            "  -b <num>   Number of block offset bits.\n"
            "  -t <file>  Trace file.\n\n"
            "Examples:\n"
            "  linux>  ./csim-ref -s 4 -E 1 -b 4 -t traces/yi.trace\n"
            "  linux>  ./csim-ref -v -s 8 -E 2 -b 4 -t traces/yi.trace\n");
}


int main(int argc, char *argv[])
{
    // init hit_count, miss_count, evict_count
    hit_count = miss_count = eviction_count = 0;
    
    int opt;    // getopt
    while ((opt = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
        switch (opt) {
            case 'h':
                h = 1;
                printHelper();
                break;
            case 'v':
                v = 1;
                printf("version: 0.1\n");
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
                strcpy(t, optarg);
                break;
            default:
                printf("cannot identify your input parameter\n");
                break;
        }
    }

    if (s <= 0 || E <= 0 || b <= 0 || t == NULL) { // wrong parameters format
        printf("wrong parameters format\n");
        return -1;
    }

    S = 1 << s; // S = 2^s;

    FILE* fp = fopen(t, "r");
    if (fp == NULL) {
        printf("cannot open file.\n");
        return -1;
    }

    init_cache();   // init cache
    parse(fp);

    fclose(fp);
    free_cache();   // free cache

    printSummary(hit_count, miss_count, eviction_count);
    return 0;
}
