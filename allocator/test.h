

struct common_b
{
	size_t size; // Taille de la zone libre
};

struct fb
{
	struct common_b common;
	struct fb *next; // Pointeur sur la prochaine zone libre
};

struct bb // Bloc occupé
{
	struct common_b common;
};

struct allocator_header
{
	size_t memory_size;		 // Taille de notre zone mémoire
	mem_fit_function_t *fit; // Fonction de fit à utiliser
	struct fb *first;		 // Pointeur sur la 1ère zone libre
};