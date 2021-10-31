#include "mem.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "test.h"

#define NB_TESTS 10

#ifdef __BIGGEST_ALIGNMENT__
#define ALIGNMENT __BIGGEST_ALIGNMENT__
#else
#define ALIGNMENT 16
#endif

/*
 * Test qui sont fait en résumé dans ce fichier
 * Test01 : allocation et désallocation simple
 * Test02 : allocation d'une taille, on vérifie si on a dans le bloc allouée
 * 			la taille donnée avec l'alignement
 * Test03 : surcharge la mémoire de plusieurs petit bloc et libère un bloc
 * 			au milieux de la zone
 * Test04 : libère 2 zones adjacentes et vérifie la fusion
 * Test05 : libère 1 zone occupée entourée de zone libre et vérifie la fusion
 */

static void *memory_addr;

static inline void *get_system_memory_addr()
{
	return memory_addr;
}

static inline struct allocator_header *get_header()
{
	struct allocator_header *h;
	h = get_system_memory_addr();
	return h;
}

int get_nb_zones_libres()
{
	int nb_zone_libre = 0;

	struct allocator_header *p = get_header();
	struct fb *fb = p->first;
	while (fb != NULL)
	{
		nb_zone_libre++;
		fb = fb->next;
	}

	return nb_zone_libre;
}

/*
 * Fonction reprise depuis le memshell.c fournis
 */
void afficher_zone(void *adresse, size_t taille, int free)
{
	printf("Zone %s, Adresse : %lu, Taille : %lu\n", free ? "libre" : "occupee",
		   adresse - get_memory_adr(), (unsigned long)taille);
}

/*
 * Fonction reprise depuis le memshell.c fournis
 */
void afficher_zone_libre(void *adresse, size_t taille, int free)
{
	if (free)
		afficher_zone(adresse, taille, 1);
}

/*
 * Test très basique où on alloue de la mémoire puis on la désalloue
 * et on regarde si la taille de la zone à été modifié avant
 * l'allocation et après la libération
 */
void test01()
{
	size_t memoire_libre_avant_alloc = get_memory_size();
	void *p1 = mem_alloc(10);

	mem_free(p1);

	size_t memoire_libre_apres_alloc = get_memory_size();

	assert(memoire_libre_apres_alloc == memoire_libre_avant_alloc);
}

/*
 * On va allouer de la mémoire et vérifier si la taille 
 * de la mémoire allouée est bien égale à la taille + l'alignement
 * On va effectuer ce test sur 2 tailles différentes 
 */
void test02()
{
	size_t const TAILLE_A_ALLOUER = 10, TAILLE_A_ALLOUER_2 = 5;

	void *p1 = mem_alloc(TAILLE_A_ALLOUER);
	struct bb *o1 = (struct bb *)((size_t)p1 - sizeof(struct bb));

	assert(o1->common.size == ((TAILLE_A_ALLOUER + sizeof(struct bb) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1)));
	mem_free(p1);

	void *p2 = mem_alloc(TAILLE_A_ALLOUER_2);
	struct bb *o2 = (struct bb *)((size_t)p2 - sizeof(struct bb));

	assert(o2->common.size == ((TAILLE_A_ALLOUER_2 + sizeof(struct bb) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1)));
	mem_free(p2);
}

/*
 * Ce test remplis la mémoire puis désalloue une zone en plein milieux de notre zone mémoire
 * pour avoir une mémoire du type : 	O O L O O O			O : Occupé	L : Libre
 */
void test03()
{
	size_t const TAILLE_A_ALLOUER = 10;

	int const iteration = ((get_memory_size() - sizeof(struct allocator_header)) / ((TAILLE_A_ALLOUER + sizeof(struct bb) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1)));
	void *pointeur_milieux_zone;

	// on remplis la mémoire autant que l'on peut
	for (int i = 0; i < iteration; ++i)
	{
		if (i == iteration / 2)
		{
			pointeur_milieux_zone = mem_alloc(TAILLE_A_ALLOUER);
		}
		else
		{
			mem_alloc(TAILLE_A_ALLOUER);
		}
	}

	mem_free(pointeur_milieux_zone);
	mem_show(afficher_zone);

	struct allocator_header *h = get_header();
	// notre premier bloc libre doit être le pointeur du milieux de zone
	// vu que c'est celui du free juste au dessus moins les métadonnées
	assert((size_t)h->first == (size_t)pointeur_milieux_zone - sizeof(struct bb));
}

/*
 * Test la fusion de 2 zones libres adjacente
 */
void test04()
{
	size_t const TAILLE_A_ALLOUER_1 = 20, TAILLE_A_ALLOUER_2 = 5, TAILLE_A_ALLOUER_3 = 30;
	void *p1 = mem_alloc(TAILLE_A_ALLOUER_1);
	void *p2 = mem_alloc(TAILLE_A_ALLOUER_2);
	void *p3 = mem_alloc(TAILLE_A_ALLOUER_3);
	void *p4 = mem_alloc(TAILLE_A_ALLOUER_2);

	mem_free(p3);
	mem_free(p2);

	// la zone libre fusionné + celle en fin de mémoire
	assert(get_nb_zones_libres() == 2);

	mem_free(p1);
	mem_free(p4);
}

/*
 * Test la libération d'une zone occupée entouré de zone libre
 * de la forme : O L X L O O		O : Occupé    L : Libre    X : zone à libérer
 */
void test05()
{
	size_t const TAILLE_A_ALLOUER = 20;
	void *ptr;
	void *arr[3];

	for (size_t i = 0; i < 6; i++)
	{
		ptr = mem_alloc(TAILLE_A_ALLOUER + i * 5);

		if (i > 0 && i <= 3)
		{
			arr[i - 1] = ptr;
		}
	}

	mem_free(arr[0]);
	assert(get_nb_zones_libres() == 2);

	mem_free(arr[2]);
	assert(get_nb_zones_libres() == 3);

	mem_free(arr[1]);
	assert(get_nb_zones_libres() == 2);

	mem_show(afficher_zone);
}

void test_reussite_tests(void (*test)(), char *test_effectue)
{
	printf("+---------------------------------------+\n");
	printf("|\t\t%s\t\t\t|\n", test_effectue);
	printf("+---------------------------------------+\n");

	test();
	printf("%s ok\n", test_effectue);

	mem_init(get_memory_adr(), get_memory_size());
}

int main(int argc, char *argv[])
{
	mem_init(get_memory_adr(), get_memory_size());

	memory_addr = get_memory_adr();

	test_reussite_tests(test01, "test01");
	test_reussite_tests(test02, "test02");
	test_reussite_tests(test03, "test03");
	test_reussite_tests(test04, "test04");
	test_reussite_tests(test05, "test05");

	// TEST OK
	return 0;
}