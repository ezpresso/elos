#ifndef LIB_BITSET_H
#define LIB_BITSET_H

typedef struct bset {
	uint8_t *bitset;
	size_t size;
} bset_t;

int bset_alloc(bset_t *set, size_t size);
void bset_init(bset_t *set, uint8_t *data, size_t size);

void bset_free(bset_t *set);
void bset_set(bset_t *set, size_t bit);
void bset_clr(bset_t *set, size_t bit);
bool bset_test(bset_t *set, size_t bit);

int bset_ffs(bset_t *bset);

int bset_alloc_bit(bset_t *bset);
void bset_free_bit(bset_t *bset, size_t bit);

#endif
