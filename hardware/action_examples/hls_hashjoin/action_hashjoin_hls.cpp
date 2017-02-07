/**
 * Simple hash-join algorithm in C.
 *
 * Example code in Python from:
 *    http://rosettacode.org/wiki/Hash_join
 *
 * Example for hashtable from:
 *    https://gist.github.com/tonious/1377667
 *
 * Bruno and Frank 2016, 2017
 *
 * Comments:
 *   - Avoid void *
 *   - Put parameter arrays on stack instead of pointer passing
 *     such that HSL can use them to generate proper interfaces
 *   - No pointer chasing - use multidimensional tables (memory ...) instead
 */

/* #define NO_SYNTH */

#if defined(NO_SYNTH)

#include <stdlib.h> /* malloc, free, atoi */
#include <stdio.h>  /* printf */
#include <limits.h> /* ULONG_MAX = 0xFFFFFFFFUL */
#include "action_hashjoin_hls.h"

#define __unused __attribute__((unused))

#else

#include <string.h>
#include "ap_int.h"
#include "action_hashjoin_hls.h"

#define __unused

/*
 * Hardware implementation is lacking some libc functions. So let us
 * replace those.
 */
#ifndef ULONG_MAX
#  define ULONG_MAX 0xFFFFFFFFUL /* gcc compiler but not HLS compiler */
#endif
#ifndef NULL
#  define NULL 0                 /* gcc compiler but not HLS compiler */
#endif

#define printf(fmt, args...)

#define MAX_NB_OF_BYTES_READ    128*128                    // Value should be X*BPERDW
ap_uint<MEMDW> buffer_mem[MAX_NB_OF_BYTES_READ/BPERDW];   // if MEMDW=512 : 128*128=>256 words

#endif  /* NO_SYNTH */

int quiet = 0;
int check = 1;

typedef char hashkey_t[64];
typedef char hashdata_t[256];

typedef struct table1_s {
        hashkey_t name;             /* 64 bytes */
        unsigned int age;           /*  4 bytes */
        unsigned int reserved[60];  /* 60 bytes */
} table1_t;

typedef struct table2_s {
        hashkey_t name;             /* 64 bytes */
        hashkey_t animal;           /* 64 bytes */
} table2_t;

typedef struct table3_s {
        hashkey_t animal;           /* 64 bytes */
        hashkey_t name;             /* 64 bytes */
        unsigned int age;           /*  4 bytes */
        unsigned int reserved[60];  /* 60 bytes */
} table3_t;

//#if defined(NO_SYNTH)
    // table1 is initialized as constant for software
  static table1_t table1[] = {
        { /* .name = */ "ronah",  /* .age = */127, { 0x0, } },
        { /* .name = */ "rlan",   /* .age = */118, { 0x0, } },
        { /* .name = */ "rlory",  /* .age = */128, { 0x0, } },
        { /* .name = */ "ropeye", /* .age = */118, { 0x0, } },
        { /* .name = */ "rlan",   /* .age = */128, { 0x0, } },
        { /* .name = */ "rlan",   /* .age = */138, { 0x0, } },
        { /* .name = */ "rlan",   /* .age = */148, { 0x0, } },
        { /* .name = */ "rlan",   /* .age = */158, { 0x0, } },
        { /* .name = */ "rdam",   /* .age = */168, { 0x0, } },
        { /* .name = */ "rnton",  /* .age = */123, { 0x0, } },
        { /* .name = */ "rnton",  /* .age = */124, { 0x0, } },
        { /* .name = */ "rieter", /* .age = */125, { 0x0, } },
        { /* .name = */ "roerg",  /* .age = */126, { 0x0, } },
        { /* .name = */ "rhomas", /* .age = */122, { 0x0, } },
        { /* .name = */ "rrank",  /* .age = */120, { 0x0, } },
        { /* .name = */ "Bruno" , /* .age = */112, { 0x0, } },
        { /* .name = */ "rlumi" , /* .age = */115, { 0x0, } },
        { /* .name = */ "rikey",  /* .age = */115, { 0x0, } },
        { /* .name = */ "rlong",  /* .age = */114, { 0x0, } },
        { /* .name = */ "riffy",  /* .age = */113, { 0x0, } },
        { /* .name = */ "riffy",  /* .age = */112, { 0x0, } },
};
//#else
//       // table1 is read from host mem for hardware
//  //#define TABLE1_SIZE 256
//  #define TABLE1_SIZE 25
//  static table1_t table1[TABLE1_SIZE] ;
//#endif
/*
 * Decouple the entries to maintain the multihash table from the data
 * in table1, since we do not want to transfer empty entries over the
 * PCIe bus to the card.
 */
//#if defined(NO_SYNTH)
       // table2 is initialized as constant for software
   static table2_t table2[] = {
        { /* .name = */ "ronah", /* .animal = */ "Whales"   },
        { /* .name = */ "ronah", /* .animal = */ "Spiders"  },
        { /* .name = */ "rlan",  /* .animal = */ "Ghosts"   },
        { /* .name = */ "rlan",  /* .animal = */ "Zombies"  },
        { /* .name = */ "rlory", /* .animal = */ "Buffy"    },
        { /* .name = */ "rrobi", /* .animal = */ "Giraffe"  },
        { /* .name = */ "roofy", /* .animal = */ "Lion"     },
        { /* .name = */ "rumie", /* .animal = */ "Gepard"   },
        { /* .name = */ "rlumi", /* .animal = */ "Cow"      },
        { /* .name = */ "roofy", /* .animal = */ "Ape"      },
        { /* .name = */ "roofy", /* .animal = */ "Fish"     },
        { /* .name = */ "rikey", /* .animal = */ "Trout"    },
        { /* .name = */ "rikey", /* .animal = */ "Greyling" },
        { /* .name = */ "rnton", /* .animal = */ "Eagle"    },
        { /* .name = */ "rhomy", /* .animal = */ "Austrich" },
        { /* .name = */ "rlomy", /* .animal = */ "Sharks"   },
        { /* .name = */ "rroof", /* .animal = */ "Fly"      },
        { /* .name = */ "rlimb", /* .animal = */ "Birds"    },
        { /* .name = */ "rlong", /* .animal = */ "Buffy"    },
        { /* .name = */ "rrank", /* .animal = */ "Turtles"  },
        { /* .name = */ "rrank", /* .animal = */ "Gorillas" },
        { /* .name = */ "roffy", /* .animal = */ "Buffy"    },
        { /* .name = */ "ruffy", /* .animal = */ "Buffy"    },
        { /* .name = */ "rrank", /* .animal = */ "Buffy"    },
        { /* .name = */ "Bruno", /* .animal = */ "Buffy"    },
};
//#else
     // table2 is read from host mem for hardware
//  //#define TABLE2_SIZE 512
//  #define TABLE2_SIZE 25
//  static table2_t table2[TABLE2_SIZE] ;
//#endif

#define HT_SIZE 128             /* size of hashtable */
#define HT_MULTI ARRAY_SIZE(table1) /* multihash entries = ARRAY_SIZE(table1) */

typedef struct entry_s {
        hashkey_t key;          /* key */
        unsigned int used;      /* list entries used */
        table1_t multi[HT_MULTI];/* fixed size */
} entry_t;
//
typedef struct hashtable_s {
        entry_t table[HT_SIZE]; /* fixed size */
} hashtable_t;

static hashtable_t hashtable;
static unsigned int table3_idx = 0;
static table3_t table3[ARRAY_SIZE(table1) * ARRAY_SIZE(table2)];


#if defined(NO_SYNTH)
#else
//---------------------------------------------------------------------
short write_burst_of_data_to_mem(ap_uint<MEMDW> *dout_gmem, ap_uint<MEMDW> *d_ddrmem,
         ap_uint<16> memory_type, ap_uint<64> output_address,
         ap_uint<MEMDW> *buffer, ap_uint<64> size_in_bytes_to_transfer);
short read_burst_of_data_from_mem(ap_uint<MEMDW> *din_gmem, ap_uint<MEMDW> *d_ddrmem,
         ap_uint<16> memory_type, ap_uint<64> input_address,
         ap_uint<MEMDW> *buffer, ap_uint<64> size_in_bytes_to_transfer);
short read_single_word_of_data_from_mem(ap_uint<MEMDW> *din_gmem, ap_uint<MEMDW> *d_ddrmem,
         ap_uint<16> memory_type, ap_uint<64> input_address, ap_uint<MEMDW> *buffer);
void convert_64charTable_to_DWTable(ap_uint<MEMDW> *buffer, char *SixtyFourBytesWordToWrite);
void convert_DWTable_to_64charTable(ap_uint<MEMDW> *buffer, char *SixtyFourBytesWordRead);
#endif


/*
 * The strcmp() function compares the two strings s1 and s2. It
 * returns an integer less than, equal to, or greater than zero if s1
 * is found, respectively, to be less than, to match, or be greater
 * than s2.
 */
int hashkey_cmp(hashkey_t s1, hashkey_t s2)
{
        size_t i;

        for (i = 0; i < sizeof(hashkey_t); i++) {
        #pragma HLS UNROLL
                if (*s1 == 0 || *s2 == 0)
                        break;

                if (*s1 != *s2)
                        return *s1 - *s2;

                s1 += 1;
                s2 += 1;
        }
        return *s1 - *s2;
}

void hashkey_cpy(hashkey_t dst, hashkey_t src)
{
        size_t i;

        for (i = 0; i < sizeof(hashkey_t); i++) {
        //#pragma HLS UNROLL
                *dst = *src;
                src++;
                dst++;
        }
}

size_t hashkey_len(hashkey_t str)
{
        size_t len;

        for (len = 0; len < sizeof(hashkey_t); len++) {
                if (*str == 0)
                        break;
                str++;
        }
        return len;
}

/* FIXME We need to use the HLS built in version instead of this */
void table1_cpy(table1_t *dest, table1_t *src)
{
        *dest = *src;
}

#if defined(NO_SYNTH)
static inline void print_hex(table1_t *buf, size_t len)
{
        unsigned int x;
        char *d = (char *)buf;

        printf("{ ");
        for (x = 0; x < len; x++)
                printf("%02x, ", d[x]);
        printf("}");
}
void ht_dump(hashtable_t *ht)
{
        unsigned int i, j;

        printf("hashtable = {\n");
        for (i = 0; i < HT_SIZE; i++) {
                entry_t *entry = &ht->table[i];

                if (!entry->used)
                        continue;

                printf("  .ht[%d].key = \"%s\" = {\n", i, entry->key);
                for (j = 0; j < entry->used; j++) {
                        table1_t *multi = &entry->multi[j];

                        printf("    { .val = { ");
                        print_hex(multi, sizeof(*multi));
                        printf(" },\n");
                }
                printf("  },\n");
        }
        printf("};\n");
}
#else
#  define ht_dump(ht)
#endif

unsigned int ht_count(hashtable_t *ht)
{
        unsigned int i, j;
        unsigned int count = 0;

        for (i = 0; i < HT_SIZE; i++) {
        //#pragma HLS UNROLL
                entry_t *entry = &ht->table[i];

                if (!entry->used)
                        continue;

                for (j = 0; j < entry->used; j++)
                        count++;
        }
        return count;
}
/* Create a new hashtable. */
void ht_init(hashtable_t *ht)
{
        unsigned int i;

        for (i = 0; i < HT_SIZE; i++) {
        //#pragma HLS UNROLL
                entry_t *entry = &ht->table[i];

                entry->used = 0;
        }
}

/* Hash a string for a particular hash table. */
int ht_hash(hashkey_t key)
{
        unsigned long int hashval = 0;
        unsigned int i;
        unsigned len = hashkey_len(key);

        /* Convert our string to an integer */
        for (i = 0; hashval < ULONG_MAX && i < len; i++) {
       //#pragma HLS UNROLL // Cannot unroll loop completely: variable loop bound.
                hashval = hashval << 8;
                hashval += key[i];
        }
        return hashval % HT_SIZE;
}

/**
 * Insert a key-value pair into a hash table.
 *
 * FIXME Review void *value and try to replace with hashdata_t ...
 *       failed on 1st try.
 *
 */
int ht_set(hashtable_t *ht, hashkey_t key,
           table1_t *value)
{
        int rc;
        unsigned int i;
        unsigned int bin = 0;

        bin = ht_hash(key);

        /* search if entry exists already */
        for (i = 0; i < HT_SIZE; i++) {
       //#pragma HLS UNROLL
                table1_t *multi;
                entry_t *entry = &ht->table[bin];

                if (entry->used == 0) { /* hey unused, we can have it */
                        hashkey_cpy(entry->key, key);
                        multi = &entry->multi[entry->used];
                        table1_cpy(multi, value);
                        entry->used++;
                        return 0;
                }

                rc = hashkey_cmp(key, entry->key);
                if (rc == 0) {          /* insert new multi */
                        if (entry->used == HT_MULTI)
                                return -1;      /* does not fit */

                        multi = &entry->multi[entry->used];
                        table1_cpy(multi, value);
                        entry->used++;
                        return 0;
                }

                /* double hash because of collision */
                if (rc != 0)            /* try next one - not smart */
                        bin = (bin + 1) % HT_SIZE;
        }

        return 0;
}

/**
 * Retrieve an array of values matching the key from a hash table.
 * Return the index and not the pointer to entry_t, since HLS does
 * not like that.
 *
 * Non-optimal double hash implementation: pick the next free entry.
 */
int ht_get(hashtable_t *ht, char *key)
{
        int rc;
        unsigned int i;
        unsigned int bin = 0;
        entry_t *entry = NULL;

        bin = ht_hash(key);

        /* search if entry exists already */
        for (i = 0; i < HT_SIZE; i++) {
       //#pragma HLS UNROLL
                entry = &ht->table[bin];

                if (entry->used == 0)   /* key not there */
                        return -1;

                rc = hashkey_cmp(key, entry->key);
                if (rc == 0)            /* good key was found */
                        return bin;

                /* double hash */
                if (rc != 0)            /* try next one - not smart */
                        bin = (bin + 1) % HT_SIZE;
        }

        return -1;
}

void table3_init(unsigned int *table3_idx)
{
        *table3_idx = 0;
}

int table3_append(table3_t *table3, unsigned int *table3_idx,
                  hashkey_t name, hashkey_t animal,
                  unsigned int age)
{
        table3_t *t3;

        t3 = &table3[*table3_idx];
        hashkey_cpy(t3->name, name);
        hashkey_cpy(t3->animal, animal);
        t3->age = age;
        *table3_idx = *table3_idx + 1;

        return *table3_idx;
}

#if defined(NO_SYNTH)
void table3_dump(table3_t *table3, unsigned int table3_idx)
{
        unsigned int i;
        table3_t *t3;

        printf("table3_t table3[] = { \n");
        for (i = 0; i < table3_idx; i++) {
                t3 = &table3[i];
                printf("  { .name = \"%s\", .animal = \"%s\", .age=%d }\n",
                       t3->name, t3->animal, t3->age);
        }
        printf("}; (%d lines)\n", table3_idx);
}
#else
short read_table1(ap_uint<64> input_address,
        ap_uint<MEMDW> *din_gmem, ap_uint<MEMDW> *d_ddrmem,
        action_input_reg *Action_Input)
{
        unsigned int i;
        short rc = 0;
        hashkey_t word_read;

        ///FIXME Need to manage the size of the buffer tightly
        rc = read_burst_of_data_from_mem(din_gmem, d_ddrmem, Action_Input->Data.t1.type,
                input_address, buffer_mem, 128*21); //Action_Input->Data.t1.size);

        for (i = 0; i < ARRAY_SIZE(table1); i++) {
        //#pragma HLS UNROLL -- unset to fit timing
                 //limitation : consider that all fields are aligned on 64 Bytes
                convert_DWTable_to_64charTable( &buffer_mem[ (i*2*WPERDW) ],
                         table1[i].name );
                table1[i].age  = (unsigned int) buffer_mem[ (i*2*WPERDW) + WPERDW ](31, 0);
        }
        return rc;
}


short read_table2(ap_uint<64> input_address,
        ap_uint<MEMDW> *din_gmem, ap_uint<MEMDW> *d_ddrmem,
        action_input_reg *Action_Input)
{
        unsigned int i;
        short rc = 0;

        ///FIXME Need to manage the size of the buffer tightly
        rc = read_burst_of_data_from_mem(din_gmem, d_ddrmem, Action_Input->Data.t2.type,
                input_address, buffer_mem, 128*25); //Action_Input->Data.t2.size);

        for (i = 0; i < ARRAY_SIZE(table2); i++) {
        //#pragma HLS UNROLL -- unset to fit timing
                convert_DWTable_to_64charTable( &buffer_mem[ (i*2*WPERDW)             ],
                         table2[i].name );
                convert_DWTable_to_64charTable( &buffer_mem[ (i*2*WPERDW)+WPERDW ],
                         table2[i].animal );

        }
        return rc;
}

short table3_dump(table3_t *table3, unsigned int table3_idx, ap_uint<64> output_address,
        ap_uint<MEMDW> *dout_gmem, ap_uint<MEMDW> *d_ddrmem,
        action_input_reg *Action_Input)
{
        unsigned int i;
        table3_t *t3;
        short rc = 0;
        ap_uint<64> current_address;

        current_address = output_address;

        for (i = 0; i < table3_idx; i++) {
        //#pragma HLS UNROLL    cannot completely unroll a loop with a variable trip count
                t3 = &table3[i];

                // Following writes are done sequentially for debug purpose (i.e. no perf)
                // A filter will reduce number of data sent back to host

                convert_64charTable_to_DWTable(buffer_mem, t3->animal);
                rc |= write_burst_of_data_to_mem(dout_gmem, d_ddrmem, Action_Input->Data.t3.type,
                        current_address, buffer_mem, 64);
                current_address += WPERDW;

                convert_64charTable_to_DWTable(buffer_mem, t3->name);
                rc |= write_burst_of_data_to_mem(dout_gmem, d_ddrmem, Action_Input->Data.t3.type,
                        current_address, buffer_mem, 64);
                current_address += WPERDW;

                // write MEMDW bits words to avoid unaligned address issue (bug#39/#45)
                buffer_mem[0]( 31, 0) = t3->age;
                buffer_mem[0](MEMDW-1,32) = 0;

                rc |= write_burst_of_data_to_mem(dout_gmem, d_ddrmem, Action_Input->Data.t3.type,
                        current_address, buffer_mem, BPERDW);
                current_address += 1;

        }
        return rc;
}

#endif

/*
 * #!/usr/bin/python
 * from collections import defaultdict
 *
 * def hashJoin(table1, index1, table2, index2):
 *     h = defaultdict(list)
 *     # hash phase
 *     for s in table1:
 *        h[s[index1]].append(s)
 *     # join phase
 *     return [(s, r) for r in table2 for s in h[r[index2]]]
 *
 * for row in hashJoin(table1, 1, table2, 0):
 *     print(row)
 *
 * Output:
 *   ((27, 'Jonah'), ('Jonah', 'Whales'))
 *   ((27, 'Jonah'), ('Jonah', 'Spiders'))
 *   ((18, 'Alan'), ('Alan', 'Ghosts'))
 *   ((28, 'Alan'), ('Alan', 'Ghosts'))
 *   ((18, 'Alan'), ('Alan', 'Zombies'))
 *   ((28, 'Alan'), ('Alan', 'Zombies'))
 *   ((28, 'Glory'), ('Glory', 'Buffy'))
 */
#if defined(NO_SYNTH)
int action_hashjoin_hls(void)
#else
short action_hashjoin_hls(ap_uint<MEMDW> *din_gmem,
			  ap_uint<MEMDW> *dout_gmem,
			  ap_uint<MEMDW> *d_ddrmem,
			  action_input_reg *Action_Input,
			  ap_uint<64> T1_address,
			  ap_uint<64> T2_address,
			  ap_uint<64> T3_address,
			  ap_uint<64> *T3_produced)
#endif
{
        unsigned int i, j;
        table1_t *t1;
        hashtable_t *h = &hashtable;
        short rc = 0;

        ht_init(h);


        /* hash phase */

#if defined(NO_SYNTH)
       // table1 is defined as constant for software
#else
       // table1 is read from host mem for hardware
        rc |= read_table1(T1_address, din_gmem, d_ddrmem, Action_Input);
#endif
        for (i = 0; i < ARRAY_SIZE(table1); i++) {
        #pragma HLS UNROLL
                t1 = &table1[i];
                printf("Inserting %s ...\n", t1->name);
                ht_set(h, t1->name, t1);
        }
        ht_dump(h);

#if defined(NO_SYNTH)
       // table2 is defined as constant for software
#else
       // table2 is read from host mem for hardware
        rc |= read_table2(T2_address, din_gmem, d_ddrmem, Action_Input);
#endif

        table3_init(&table3_idx);
        for (i = 0; i < ARRAY_SIZE(table2); i++) {
        //for (i = 0; i < 25; i++) {
        #pragma HLS UNROLL
                int bin;
                entry_t *entry;
                table2_t *t2 = &table2[i];

                bin = ht_get(h, t2->name);
                if (bin == -1)
                        continue;       /* nothing found */

                entry = &h->table[bin];
                for (j = 0; j < entry->used; j++) {
                //#pragma HLS UNROLL
                        table1_t *m = &entry->multi[j];

                        table3_append(table3, &table3_idx,
                                      t2->name, t2->animal, m->age);
                }
        }

        if (!quiet) {
                //ht_dump(h); //commented this line since dump already done above
#if defined(NO_SYNTH)
                table3_dump(table3, table3_idx);
#else
                // write table 3 back to the host memory
                rc = table3_dump(table3, table3_idx, T3_address,
                        dout_gmem, d_ddrmem, Action_Input);

                *T3_produced = (ap_uint<32>) table3_idx;
#endif
        }
        /*
         * Sanity check, elements in multihash table must match
         * elements in table1.
         */
        if (check)
                return ht_count(h) != ARRAY_SIZE(table1);

        return rc;
}
