// Linear congruential generator
// https://en.wikipedia.org/wiki/Linear_congruential_generator

#define IA 3877
#define IC 29573

static unsigned int s_random_seed = 42;

void lcg_set_random_seed(unsigned int seed)
{
	s_random_seed = seed;
}

unsigned int lcg_get_random_seed(void)
{
	return s_random_seed;
}

unsigned int lcg_random(void)
{
	s_random_seed = s_random_seed * IA + IC;
	return s_random_seed;
}
