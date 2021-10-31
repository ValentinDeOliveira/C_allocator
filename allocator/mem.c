/* On inclut l'interface publique */
#include "mem.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Définition de l'alignement recherché
 * Avec gcc, on peut utiliser __BIGGEST_ALIGNMENT__
 * sinon, on utilise 16 qui conviendra aux plateformes qu'on cible
 */
#ifdef __BIGGEST_ALIGNMENT__
#define ALIGNMENT __BIGGEST_ALIGNMENT__
#else
#define ALIGNMENT 16
#endif

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

/* structure placée au début de la zone de l'allocateur

   Elle contient toutes les variables globales nécessaires au
   fonctionnement de l'allocateur

   Elle peut bien évidemment être complétée
*/
struct allocator_header
{
	size_t memory_size;		 // Taille de notre zone mémoire
	mem_fit_function_t *fit; // Fonction de fit à utiliser
	struct fb *first;		 // Pointeur sur la 1ère zone libre
};

/* La seule variable globale autorisée
 * On trouve à cette adresse le début de la zone à gérer
 * (et une structure 'struct allocator_header)
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

static inline size_t get_system_memory_size()
{
	return get_header()->memory_size;
}

/**
 * Permet de récuperer le bloc libre se trouvant avant le  
 * bloc libre passé en paramètre 
 * A utiliser quand on fait l'allocation pour pouvoir mettre 
 * le next du bloc précédent
 */
struct fb *get_prev_free_bloc(struct fb *block)
{
	struct allocator_header *p = get_header();
	struct fb *prev = p->first;

	while ((size_t)prev < (size_t)block)
	{
		if ((size_t)prev->next >= (size_t)block)
			break;
		prev = prev->next;
	}
	return prev;
}

void mem_init(void *mem, size_t taille)
{
	memory_addr = mem;
	*(size_t *)memory_addr = taille;

	/* On vérifie qu'on a bien enregistré les infos et qu'on
	 * sera capable de les récupérer par la suite */
	assert(mem == get_system_memory_addr());
	assert(taille == get_system_memory_size());

	struct allocator_header *h;
	h = (struct allocator_header *)mem;

	// On définit la taille de la zone mémoire avec laquelle on travaillera
	h->memory_size = taille;
	// On définit la fonction de fit à utiliser, dans notre cas on utilise un fit first
	h->fit = &mem_fit_first;
	// le pointeur sur le 1er bloc libre se trouve à l'adresse mémoire
	// que l'on décale par la taille de notre allocator_header
	h->first = (struct fb *)(mem + sizeof(struct allocator_header));

	// On récupère ladite zone libre
	struct fb *zonelibre = h->first;

	// Et on définit sa taille comme étant la taille donnée en paramètre
	// auquel on soustrait la taille des métadonnées
	zonelibre->common.size = taille - sizeof(struct allocator_header);

	// Il n'y a pas de prochaine zone libre donc null
	zonelibre->next = NULL;

	mem_fit(&mem_fit_first);
}

void mem_show(void (*print)(void *, size_t, int))
{
	struct allocator_header *h = get_system_memory_addr();

	// On définit un bloc se situant à l'adresse de la zone de données + la taille de
	// la structure qui se trouve au début de la zone => on a un pointeur sur le 1er bloc
	struct common_b *block = (struct common_b *)(get_system_memory_addr() + sizeof(struct allocator_header));

	// On récupère le 1er bloc libre de la zone
	struct fb *libre = h->first;

	// Pour calculer l'adresse de fin, on prend l'adresse de début de zone auquel on ajoute
	// la taille de la zone + les métadonnées
	size_t addr_fin = (size_t)((size_t)h + h->memory_size);

	// On itère tous les blocs
	while ((size_t)block < addr_fin)
	{

		// Si l'addresse du bloc libre est la même que celle du bloc que l'on itère: il est libre
		if ((size_t)libre == (size_t)block)
		{
			print(block, (block->size), 1);
			libre = libre->next;
		}
		// Sinon le bloc est occupé
		else
		{
			print(block, block->size, 0);
		}
		//On décale le bloc par sa taille pour passer au prochain bloc
		block = (struct common_b *)((size_t)block + block->size);
	}
}

void mem_fit(mem_fit_function_t *f)
{
	get_header()->fit = f;
}

void *mem_alloc(size_t taille)
{
	__attribute__((unused)) /* juste pour que gcc compile ce squelette avec -Werror */
	struct allocator_header *p = get_header();

	// On décide de d'ajouter à la taille d'un bloc la taille de ses métadonnées pour
	// faciliter l'affichage dans le mem_show
	taille += (size_t)sizeof(struct bb *);

	/* On aligne la taille sur le premier multiple de l'alignement          
         * défini par notre allocateur pour que ses structures restent alignées
        */
	taille = (taille + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);

	// Avoir le bloc libre de la taille voulu + métadonnées
	struct fb *fb = get_header()->fit(p->first, taille);

	// Pas de bloc libre
	if (fb == NULL)
	{
		return NULL;
	}
	//-> On a bien trouvé un bloc libre

	// On récupère la taille de la zone libre sur laquelle on va faire l'allocation;
	size_t taille_zone_libre = fb->common.size;

	// On récupère le next du bloc libre courant
	struct fb *next_bloc_libre = fb->next;

	// Le bloc occupé se trouve au début du bloc libre
	struct bb *new = (struct bb *)fb;

	// Taille du bloc occupé = taille donnée + taille des métadonnées
	new->common.size = (size_t)(taille);

	// Si la zone libre que l'on a allouée fait exactement la
	// même taille que celle demandée par l'utilisateur + métadonnées
	if (taille_zone_libre - new->common.size == 0)
	{
		// On récupère le bloc libre avant ce bloc libre
		struct fb *prev = get_prev_free_bloc(fb);
		// Son prochain bloc libre ne sera pas fb mais le next de fb
		prev->next = next_bloc_libre;
		// la zone n'est plus libre
		fb = NULL;
	}

	// Il y aura encore un bout de zone libre après avoir fais l'allocation
	else
	{
		// Le début de la zone libre a changé suite à l'allocation
		size_t addr_fb = (size_t)fb;
		fb = (struct fb *)(addr_fb + new->common.size);
		// La taille de la zone libre à été réduite
		fb->common.size = (size_t)(taille_zone_libre - new->common.size);
		fb->next = next_bloc_libre;
	}

	// si l'addresse du bloc allouée est la même que l'adresse
	// du 1er bloc libre de la zone
	if ((size_t)p->first == (size_t) new)
	{
		// Le premier bloc libre de la zone a changé
		p->first = fb;
	}

	// On retourne la zone d'écriture utilisateur (sans les métadonnées)
	return (void *)((size_t) new + sizeof(struct bb));
}

//fusionne les zones libre adjacentes
void fusion(struct fb *p1, struct fb *p2, struct fb *new)
{
	//place la nouvelle zone libre après p1 et avant p2

	// new se trouve entre p1 et p2
	if ((size_t) new == (size_t)p1 + p1->common.size &&
		((size_t) new + new->common.size == (size_t)p2))
	{
		new = (struct fb *)p1;

		new->common.size += p1->common.size;
		new->common.size += p2->common.size;

		new->next = p2->next;

		p1 = NULL;
		p2 = NULL;
	}

	//si la zone new et p1 sont collé
	else if ((size_t) new + new->common.size == (size_t)p1)
	{
		//la prochaine zone libre pointé par new sera celle de p2
		//on ajoute à la taille de la nouvelle zone la taille de la zone libre se trouvant juste après
		new->common.size += p1->common.size;

		//Le bloc qui vient d'être libérer est juste avant le 1er bloc libre
		if ((size_t)p2 == (size_t) new)
		{
			new->next = p1->next;
		}

		else if (p2 != NULL && p2->next != NULL)
		{
			new->next = p2->next;
		}
		else
		{
			new->next = NULL;
		}
		p1 = NULL;
	}
}

void mem_free(void *mem)
{
	// On récupère le bloc occupée à libérer
	struct bb *bloc_a_liberer = (struct bb *)(mem - sizeof(struct bb));
	// on récupère la taille du bloc
	size_t taille = bloc_a_liberer->common.size;

	// On définit un bloc libre qui commence
	// à la zone occupée à libérer
	struct fb *new = (struct fb *)bloc_a_liberer;
	// la taille de la zone libre est celle de la zone à libérer
	new->common.size = taille;

	// la zone n'est plus occupée
	bloc_a_liberer = NULL;

	//crée une zone libre de mémoire à l'endroit pointé par mem
	struct allocator_header *p = get_header();
	struct fb *p1 = p->first;
	struct fb *p2 = p1->next;

	// si le block occupé se trouve avant le 1er block libre
	if ((size_t)((size_t) new + new->common.size) == (size_t)p1)
	{
		p2 = new;
	}

	// si le bloc occupé se trouve entre 2 blocks libres
	else if (p1 != NULL && p2 != NULL)
	{
		//recherche de les deux zone libres qui entoure la nouvelle zone libre
		while (p2 != NULL && (size_t) new > (size_t)p2)
		{
			p2 = p2->next;
			p1 = p1->next;
		}
	}

	if (((size_t)p1 + p1->common.size) != (size_t) new &&
		((size_t) new + new->common.size) != (size_t)p2 &&
		(size_t)p2 != (size_t) new)
	{
		if ((size_t) new < (size_t)p1)
		{
			new->next = p1;
		}
		else
		{
			p1->next = new;
			new->next = p2;
		}
		//p1->next = new;
	}
	else
	{
		fusion(p1, p2, new);
	}
	// si l'addresse du bloc allouée est la même que l'adresse
	// du 1er bloc libre de la zone
	if (((size_t)p->first - taille) >= (size_t) new)
	{
		// Le premier bloc libre de la zone a changé
		p->first = new;
	}
}

struct fb *mem_fit_first(struct fb *list, size_t size)
{
	if (size <= 0)
	{
		return NULL;
	}
	while (list != NULL)
	{
		// Si la taille de la zone que l'on est en train d'itérer est >= à la taille demandé (+ metadonnées)
		if (list->common.size >= size + sizeof(struct bb))
		{
			return list;
		}
		list = list->next;
	}
	return NULL;
}

/* Fonction à faire dans un second temps
 * - utilisée par realloc() dans malloc_stub.c
 * - nécessaire pour remplacer l'allocateur de la libc
 * - donc nécessaire pour 'make test_ls'
 * Lire malloc_stub.c pour comprendre son utilisation
 * (ou en discuter avec l'enseignant)
 */
size_t mem_get_size(void *zone)
{

	/* zone est une adresse qui a été retournée par mem_alloc() */

	/* la valeur retournée doit être la taille maximale que
	 * l'utilisateur peut utiliser dans cette zone */
	return 0;
}

/* Fonctions facultatives
 * autres stratégies d'allocation
 */
struct fb *mem_fit_best(struct fb *list, size_t size)
{
	return NULL;
}

struct fb *mem_fit_worst(struct fb *list, size_t size)
{
	return NULL;
}