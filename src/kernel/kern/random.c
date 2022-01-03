#include <kern/system.h>
#include <kern/random.h>
#include <kern/sync.h>
#include <kern/symbol.h>

static sync_t krand_lock = SYNC_INIT(MUTEX);

/*
 * Taken from http://stackoverflow.com/questions/1167253/implementation-of-rand
 * Implementation of a 32-bit KISS generator which uses no multiply instructions
 */
static unsigned int kiss_rng(void) {
	static unsigned int x = 123456789, y = 234567891, z = 345678912,
		w = 456789123, c = 0;
	int t;

	y ^= (y << 5);
	y ^= (y >> 7);
	y ^= (y << 22);

	t = z + w + c;
	z = w;
	c = t < 0;
	w = t & 2147483647; 
	x += 1411392427; 

	return x + y + w; 
}

uint8_t krand(void) {
	uint8_t res;

	sync_scope_acquire(&krand_lock);

	/*
	 * Xor 2 simple random number generators in hope to get a better one.
	 */
	res = kiss_rng() & 0xff;

	return res;
}
export(krand);
