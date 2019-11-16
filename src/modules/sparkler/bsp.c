#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "bsp.h"
#include "chunker.h"


/* octree-based bsp for faster proximity searches */
/* meanings:
 *  octrant = "octo" analog of a quadrant, an octree is a quadtree with an additional dimension (Z/3d)
 *  bv = bounding volume
 *  bsp = binary space partition
 *  occupant = the things being indexed by the bsp (e.g. a particle, or its position)
 */


/* FIXME: these are not tuned at all, and should really all be parameters to bsp_new() instead */
#define BSP_GROWBY		16
#define BSP_MAX_OCCUPANTS	64
#define BSP_MAX_DEPTH		16

#define MAX(_a, _b)	(_a > _b ? _a : _b)
#define MIN(_a, _b)	(_a < _b ? _a : _b)


struct bsp_node_t {
	v3f_t		center;		/* center point about which the bounding volume's 3d-space is divided */
	bsp_node_t	*parent;	/* parent bounding volume, NULL when root node */
	bsp_node_t	*octrants;	/* NULL when a leaf, otherwise an array of 8 bsp_node_t's */
	list_head_t	occupants;	/* list of occupants in this volume when a leaf node */
	unsigned	n_occupants;	/* number of ^^ */
};

#define OCTRANTS					\
	octrant(OCT_XL_YL_ZL, (1 << 2 | 1 << 1 | 1))	\
	octrant(OCT_XR_YL_ZL, (         1 << 1 | 1))	\
	octrant(OCT_XL_YR_ZL, (1 << 2          | 1))	\
	octrant(OCT_XR_YR_ZL, (                  1))	\
	octrant(OCT_XL_YL_ZR, (1 << 2 | 1 << 1    ))	\
	octrant(OCT_XR_YL_ZR, (         1 << 1    ))	\
	octrant(OCT_XL_YR_ZR, (1 << 2             ))	\
	octrant(OCT_XR_YR_ZR, 0)

#define octrant(_sym, _val)	_sym = _val,
typedef enum _octrant_idx_t {
	OCTRANTS
} octrant_idx_t;
#undef octrant

/* bsp lookup state, encapsulated for preservation across composite
 * lookup-dependent operations, so they can potentially avoid having
 * to redo the lookup.  i.e. lookup caching.
 */
typedef struct _bsp_lookup_t {
	int		depth;
	v3f_t		left;
	v3f_t		right;
	bsp_node_t	*bv;
	octrant_idx_t	oidx;
} bsp_lookup_t;

struct bsp_t {
	bsp_node_t	root;
	chunker_t	*chunker;
	bsp_lookup_t	lookup_cache;
};


static inline const char * octstr(octrant_idx_t oidx)
{
#define octrant(_sym, _val)	#_sym,
	static const char	*octrant_strs[] = {
					OCTRANTS
				};
#undef octrant

	return octrant_strs[oidx];
}


static inline void _bsp_print(bsp_node_t *node)
{
	static int	depth = 0;

	fprintf(stderr, "%-*s %i: %p\n", depth, " ", depth, node);
	if (node->octrants) {
		int	i;

		for (i = 0; i < 8; i++) {
			fprintf(stderr, "%-*s %i: %s: %p\n", depth, " ", depth, octstr(i), &node->octrants[i]);
			depth++;
			_bsp_print(&node->octrants[i]);
			depth--;
		}
	}
}


/* Print a bsp tree to stderr (debugging) */
void bsp_print(bsp_t *bsp)
{
	_bsp_print(&bsp->root);
}


/* Initialize the lookup cache to the root */
static inline void bsp_init_lookup_cache(bsp_t *bsp) {
	bsp->lookup_cache.bv = &bsp->root;
	bsp->lookup_cache.depth = 0;
	v3f_set(&bsp->lookup_cache.left, -1.0, -1.0, -1.0);	/* TODO: the bsp AABB should be supplied to bsp_new() */
	v3f_set(&bsp->lookup_cache.right, 1.0, 1.0, 1.0);
}


/* Invalidate/reset the bsp's lookup cache TODO: make conditional on a supplied node being cached? */
static inline void bsp_invalidate_lookup_cache(bsp_t *bsp) {
	if (bsp->lookup_cache.bv != &bsp->root) {
		bsp_init_lookup_cache(bsp);
	}
}


/* Create a new bsp octree. */
bsp_t * bsp_new(void)
{
	bsp_t	*bsp;

	bsp = calloc(1, sizeof(bsp_t));
	if (!bsp) {
		return NULL;
	}

	bsp->chunker = chunker_new(sizeof(bsp_node_t) * 8 *  8);
	if (!bsp->chunker) {
		free(bsp);
		return NULL;
	}

	INIT_LIST_HEAD(&bsp->root.occupants);
	bsp_init_lookup_cache(bsp);

	return bsp;
}


/* Free a bsp octree */
void bsp_free(bsp_t *bsp)
{
	chunker_free_chunker(bsp->chunker);
	free(bsp);
}


/* lookup a position's containing leaf node in the bsp tree, store resultant lookup state in *lookup_res */
static inline void bsp_lookup_position(bsp_t *bsp, bsp_node_t *root, v3f_t *position, bsp_lookup_t *lookup_res)
{
	bsp_lookup_t	res = bsp->lookup_cache;

	if (res.bv->parent) {
		/* When starting from a cached (non-root) lookup, we must verify our position falls within the cached bv */
		if (position->x < res.left.x || position->x > res.right.x ||
		    position->y < res.left.y || position->y > res.right.y ||
		    position->z < res.left.z || position->z > res.right.z) {
			bsp_invalidate_lookup_cache(bsp);
			res = bsp->lookup_cache;
		}
	}

	while (res.bv->octrants) {
		res.oidx = OCT_XR_YR_ZR;
		if (position->x <= res.bv->center.x) {
			res.oidx |= (1 << 2);
			res.right.x = res.bv->center.x;
		} else {
			res.left.x = res.bv->center.x;
		}

		if (position->y <= res.bv->center.y) {
			res.oidx |= (1 << 1);
			res.right.y = res.bv->center.y;
		} else {
			res.left.y = res.bv->center.y;
		}

		if (position->z <= res.bv->center.z) {
			res.oidx |= 1;
			res.right.z = res.bv->center.z;
		} else {
			res.left.z = res.bv->center.z;
		}

		res.bv = &res.bv->octrants[res.oidx];
		res.depth++;
	}

	*lookup_res = bsp->lookup_cache = res;
}


/* Add an occupant to a bsp tree, use provided node lookup *l if supplied */
static inline void _bsp_add_occupant(bsp_t *bsp, bsp_occupant_t *occupant, v3f_t *position, bsp_lookup_t *l)
{
	bsp_lookup_t	_lookup;

	/* if no explicitly cached lookup result was provided, perform the lookup now (which may still be cached). */
	if (!l) {
		l = &_lookup;
		bsp_lookup_position(bsp, &bsp->root, position, l);
	}

	assert(l);
	assert(l->bv);

	occupant->position = position;

#define map_occupant2octrant(_occupant, _bv, _octrant)		\
		_octrant = OCT_XR_YR_ZR;			\
		if (_occupant->position->x <= _bv->center.x) {	\
			_octrant |= (1 << 2);			\
		}						\
		if (_occupant->position->y <= _bv->center.y) {	\
			_octrant |= (1 << 1);			\
		}						\
		if (_occupant->position->z <= _bv->center.z) {	\
			_octrant |= 1;				\
		}

	if (l->bv->n_occupants >= BSP_MAX_OCCUPANTS && l->depth < BSP_MAX_DEPTH) {
		int		i;
		list_head_t	*t, *_t;
		bsp_node_t	*bv = l->bv;

		/* bv is full and shallow enough, subdivide it. */
		bv->octrants = chunker_alloc(bsp->chunker, sizeof(bsp_node_t) * 8);

		/* initialize the octrants */
		for (i = 0; i < 8; i++) {
			INIT_LIST_HEAD(&bv->octrants[i].occupants);
			bv->octrants[i].n_occupants = 0;
			bv->octrants[i].parent = bv;
			bv->octrants[i].octrants = NULL;
		}

		/* set the center point in each octrant which places the partitioning hyperplane */
		/* XXX: note this is pretty unreadable due to reusing the earlier computed values
		 * where the identical computation is required.
		 */
		bv->octrants[OCT_XR_YR_ZR].center.x = (l->right.x - bv->center.x) * .5f + bv->center.x;
		bv->octrants[OCT_XR_YR_ZR].center.y = (l->right.y - bv->center.y) * .5f + bv->center.y;
		bv->octrants[OCT_XR_YR_ZR].center.z = (l->right.z - bv->center.z) * .5f + bv->center.z;

		bv->octrants[OCT_XR_YR_ZL].center.x = bv->octrants[OCT_XR_YR_ZR].center.x;
		bv->octrants[OCT_XR_YR_ZL].center.y = bv->octrants[OCT_XR_YR_ZR].center.y;
		bv->octrants[OCT_XR_YR_ZL].center.z = (bv->center.z - l->left.z) * .5f + l->left.z;

		bv->octrants[OCT_XR_YL_ZR].center.x = bv->octrants[OCT_XR_YR_ZR].center.x;
		bv->octrants[OCT_XR_YL_ZR].center.y = (bv->center.y - l->left.y) * .5f + l->left.y;
		bv->octrants[OCT_XR_YL_ZR].center.z = bv->octrants[OCT_XR_YR_ZR].center.z;

		bv->octrants[OCT_XR_YL_ZL].center.x = bv->octrants[OCT_XR_YR_ZR].center.x;
		bv->octrants[OCT_XR_YL_ZL].center.y = bv->octrants[OCT_XR_YL_ZR].center.y;
		bv->octrants[OCT_XR_YL_ZL].center.z = bv->octrants[OCT_XR_YR_ZL].center.z;

		bv->octrants[OCT_XL_YR_ZR].center.x = (bv->center.x - l->left.x) * .5f + l->left.x;
		bv->octrants[OCT_XL_YR_ZR].center.y = bv->octrants[OCT_XR_YR_ZR].center.y;
		bv->octrants[OCT_XL_YR_ZR].center.z = bv->octrants[OCT_XR_YR_ZR].center.z;

		bv->octrants[OCT_XL_YR_ZL].center.x = bv->octrants[OCT_XL_YR_ZR].center.x;
		bv->octrants[OCT_XL_YR_ZL].center.y = bv->octrants[OCT_XR_YR_ZR].center.y;
		bv->octrants[OCT_XL_YR_ZL].center.z = bv->octrants[OCT_XR_YR_ZL].center.z;

		bv->octrants[OCT_XL_YL_ZR].center.x = bv->octrants[OCT_XL_YR_ZR].center.x;
		bv->octrants[OCT_XL_YL_ZR].center.y = bv->octrants[OCT_XR_YL_ZR].center.y;
		bv->octrants[OCT_XL_YL_ZR].center.z = bv->octrants[OCT_XR_YR_ZR].center.z;

		bv->octrants[OCT_XL_YL_ZL].center.x = bv->octrants[OCT_XL_YR_ZR].center.x;
		bv->octrants[OCT_XL_YL_ZL].center.y = bv->octrants[OCT_XR_YL_ZR].center.y;
		bv->octrants[OCT_XL_YL_ZL].center.z = bv->octrants[OCT_XR_YR_ZL].center.z;

		/* migrate the occupants into the appropriate octrants */
		list_for_each_safe(t, _t, &bv->occupants) {
			octrant_idx_t	oidx;
			bsp_occupant_t	*o = list_entry(t, bsp_occupant_t, occupants);

			map_occupant2octrant(o, bv, oidx);
			list_move(t, &bv->octrants[oidx].occupants);
			o->leaf = &bv->octrants[oidx];
			bv->octrants[oidx].n_occupants++;
		}
		bv->n_occupants = 0;

		/* a new leaf assumes the bv position for the occupant to be added into */
		map_occupant2octrant(occupant, bv, l->oidx);
		l->bv = &bv->octrants[l->oidx];
		l->depth++;
	}

#undef map_occupant2octrant

	occupant->leaf = l->bv;
	list_add(&occupant->occupants, &l->bv->occupants);
	l->bv->n_occupants++;

	assert(occupant->leaf);
}


/* add an occupant to a bsp tree */
void bsp_add_occupant(bsp_t *bsp, bsp_occupant_t *occupant, v3f_t *position)
{
	_bsp_add_occupant(bsp, occupant, position, NULL);
}


/* Delete an occupant from a bsp tree.
 * Set reservation to prevent potentially freeing a node made empty by our delete that
 * we have a reference to (i.e. a cached lookup result, see bsp_move_occupant()).
 */
static inline void _bsp_delete_occupant(bsp_t *bsp, bsp_occupant_t *occupant, bsp_node_t *reservation)
{
	if (occupant->leaf->octrants) {
		fprintf(stderr, "BUG: deleting occupant(%p) from non-leaf bv(%p)\n", occupant, occupant->leaf);
	}

	/* delete the occupant */
	list_del(&occupant->occupants);
	occupant->leaf->n_occupants--;

	if (list_empty(&occupant->leaf->occupants)) {
		bsp_node_t	*parent_bv;

		if (occupant->leaf->n_occupants) {
			fprintf(stderr, "BUG: bv_occupants empty but n_occupants=%u\n", occupant->leaf->n_occupants);
		}

		/* leaf is now empty, since nodes are allocated as clusters of 8, they aren't freed unless all nodes in the cluster are empty.
		 * Determine if they're all empty, and free the parent's octrants as a set.
		 * Repeat this process up the chain of parents, repeatedly converting empty parents into leaf nodes.
		 * TODO: maybe just use the chunker instead?
		 */

		for (parent_bv = occupant->leaf->parent; parent_bv && parent_bv != reservation; parent_bv = parent_bv->parent) {
			int	i;

			/* are _all_ the parent's octrants freeable? */
			for (i = 0; i < 8; i++) {
				if (&parent_bv->octrants[i] == reservation ||
				    parent_bv->octrants[i].octrants ||
				    !list_empty(&parent_bv->octrants[i].occupants)) {
					goto _out;
				}
			}

			/* "freeing" really just entails putting the octrants cluster of nodes onto the free list */
			chunker_free(parent_bv->octrants);
			parent_bv->octrants = NULL;
			bsp_invalidate_lookup_cache(bsp);
		}
	}

_out:
	occupant->leaf = NULL;
}


/* Delete an occupant from a bsp tree. */
void bsp_delete_occupant(bsp_t *bsp, bsp_occupant_t *occupant)
{
	_bsp_delete_occupant(bsp, occupant, NULL);
}


/* Move an occupant within a bsp tree to a new position */
void bsp_move_occupant(bsp_t *bsp, bsp_occupant_t *occupant, v3f_t *position)
{
	bsp_lookup_t	lookup_res;

	if (v3f_equal(occupant->position, position)) {
		return;
	}

	/* TODO: now that there's a cache maintained in bsp->lookup_cache as well,
	 * this feels a bit vestigial, see about consolidating things.  We still
	 * need to be able to pin lookup_res.bv in the delete, but why not just use
	 * the one in bsp->lookup_cache.bv then stop having lookup_position return
	 * a result at all????  this bsp isn't concurrent/threaded, so it doens't
	 * really matter.
	 */
	bsp_lookup_position(bsp, &bsp->root, occupant->position, &lookup_res);
	if (lookup_res.bv == occupant->leaf) {
		/* leaf unchanged, do nothing past lookup. */
		occupant->position = position;
		return;
	}

	_bsp_delete_occupant(bsp, occupant, lookup_res.bv);
	_bsp_add_occupant(bsp, occupant, position, &lookup_res);
}


static inline float square(float v)
{
	return v * v;
}


typedef enum overlaps_t {
	OVERLAPS_NONE,		/* objects are completely separated */
	OVERLAPS_PARTIALLY,	/* objects surfaces one another */
	OVERLAPS_A_IN_B,	/* first object is fully within the second */
	OVERLAPS_B_IN_A,	/* second object is fully within the first */
} overlaps_t;


/* Returns wether the axis-aligned bounding box (AABB) overlaps the sphere.
 * Absolute vs. partial overlaps are distinguished, since it's an important optimization
 * to know if the sphere falls entirely within one partition of the octree.
 */
static inline overlaps_t aabb_overlaps_sphere(v3f_t *aabb_min, v3f_t *aabb_max, v3f_t *sphere_center, float sphere_radius)
{
	/* This implementation is based on James Arvo's from Graphics Gems pg. 335 */
	float	r2 = square(sphere_radius);
	float	dface = INFINITY;
	float	dmin = 0;
	float	dmax = 0;
	float	a, b;

#define per_dimension(_center, _box_max, _box_min)		\
	a = square(_center - _box_min);				\
	b = square(_center - _box_max);				\
								\
	dmax += MAX(a, b);					\
	if (_center >= _box_min && _center <= _box_max) {	\
		/* sphere center within box */			\
		dface = MIN(dface, MIN(a, b));			\
	} else {						\
		/* sphere center outside the box */		\
		dface = 0;					\
		dmin += MIN(a, b);				\
	}

	per_dimension(sphere_center->x, aabb_max->x, aabb_min->x);
	per_dimension(sphere_center->y, aabb_max->y, aabb_min->y);
	per_dimension(sphere_center->z, aabb_max->z, aabb_min->z);

	if (dmax < r2) {
		/* maximum distance to box smaller than radius, box is inside
		 * the sphere */
		return OVERLAPS_A_IN_B;
	}

	if (dface > r2) {
		/* sphere center is within box (non-zero dface), and dface is
		 * greater than sphere diameter, sphere is inside the box. */
		return OVERLAPS_B_IN_A;
	}

	if (dmin <= r2) {
		/* minimum distance from sphere center to box is smaller than
		 * sphere's radius, surfaces intersect */
		return OVERLAPS_PARTIALLY;
	}

	return OVERLAPS_NONE;
}


typedef struct bsp_search_sphere_t {
	v3f_t	*center;
	float	radius_min;
	float	radius_max;
	void	(*cb)(bsp_t *, list_head_t *, void *);
	void	*cb_data;
} bsp_search_sphere_t;


static overlaps_t _bsp_search_sphere(bsp_t *bsp, bsp_node_t *node, bsp_search_sphere_t *search, v3f_t *aabb_min, v3f_t *aabb_max)
{
	overlaps_t	res;
	v3f_t		oaabb_min, oaabb_max;

	/* if the radius_max search doesn't overlap aabb_min:aabb_max at all, simply return. */
	res = aabb_overlaps_sphere(aabb_min, aabb_max, search->center, search->radius_max);
	if (res == OVERLAPS_NONE) {
		return res;
	}

	/* if the radius_max absolutely overlaps the AABB, we must see if the AABB falls entirely within radius_min so we can skip it. */
	if (res == OVERLAPS_A_IN_B) {
		res = aabb_overlaps_sphere(aabb_min, aabb_max, search->center, search->radius_min);
		if (res == OVERLAPS_A_IN_B) {
			/* AABB is entirely within radius_min, skip it. */
			return OVERLAPS_NONE;
		}

		if (res == OVERLAPS_NONE) {
			/* radius_min didn't overlap, radius_max overlapped aabb 100%, it's entirely within the range. */
			res = OVERLAPS_A_IN_B;
		} else {
			/* radius_min overlapped partially.. */
			res = OVERLAPS_PARTIALLY;
		}
	}

	/* if node is a leaf, call search->cb with the occupants, then return. */
	if (!node->octrants) {
		search->cb(bsp, &node->occupants, search->cb_data);
		return res;
	}

	/* node is a parent, recur on each octrant with appropriately adjusted aabb_min:aabb_max values */
	/* if any of the octrants absolutely overlaps the search sphere, skip the others by returning. */
#define search_octrant(_oid, _aabb_min, _aabb_max)						\
	res = _bsp_search_sphere(bsp, &node->octrants[_oid], search, _aabb_min, _aabb_max);	\
	if (res == OVERLAPS_B_IN_A) {								\
		return res;									\
	}

	/* OCT_XL_YL_ZL and OCT_XR_YR_ZR AABBs don't require tedious composition */
	search_octrant(OCT_XL_YL_ZL, aabb_min, &node->center);
	search_octrant(OCT_XR_YR_ZR, &node->center, aabb_max);

	/* the rest are stitched together requiring temp storage and tedium */
	v3f_set(&oaabb_min, node->center.x, aabb_min->y, aabb_min->z);
	v3f_set(&oaabb_max, aabb_max->x, node->center.y, node->center.z);
	search_octrant(OCT_XR_YL_ZL, &oaabb_min, &oaabb_max);

	v3f_set(&oaabb_min, aabb_min->x, node->center.y, aabb_min->z);
	v3f_set(&oaabb_max, node->center.x, aabb_max->y, node->center.z);
	search_octrant(OCT_XL_YR_ZL, &oaabb_min, &oaabb_max);

	v3f_set(&oaabb_min, node->center.x, node->center.y, aabb_min->z);
	v3f_set(&oaabb_max, aabb_max->x, aabb_max->y, node->center.z);
	search_octrant(OCT_XR_YR_ZL, &oaabb_min, &oaabb_max);

	v3f_set(&oaabb_min, aabb_min->x, aabb_min->y, node->center.z);
	v3f_set(&oaabb_max, node->center.x, node->center.y, aabb_max->z);
	search_octrant(OCT_XL_YL_ZR, &oaabb_min, &oaabb_max);

	v3f_set(&oaabb_min, node->center.x, aabb_min->y, node->center.z);
	v3f_set(&oaabb_max, aabb_max->x, node->center.y, aabb_max->z);
	search_octrant(OCT_XR_YL_ZR, &oaabb_min, &oaabb_max);

	v3f_set(&oaabb_min, aabb_min->x, node->center.y, node->center.z);
	v3f_set(&oaabb_max, node->center.x, aabb_max->y, aabb_max->z);
	search_octrant(OCT_XL_YR_ZR, &oaabb_min, &oaabb_max);

#undef search_octrant

	/* since early on an OVERLAPS_NONE short-circuits the function, and
	 * OVERLAPS_ABSOLUTE also causes short-circuits, if we arrive here it's
	 * a partial overlap
	 */
	return OVERLAPS_PARTIALLY;
}


/* search the bsp tree for leaf nodes which intersect the space between radius_min and radius_max of a sphere @ center */
/* for every leaf node found to intersect the sphere, cb is called with the leaf node's occupants list head */
/* the callback cb must then further filter the occupants as necessary. */
void bsp_search_sphere(bsp_t *bsp, v3f_t *center, float radius_min, float radius_max, void (*cb)(bsp_t *, list_head_t *, void *), void *cb_data)
{
	bsp_search_sphere_t	search = {
					.center = center,
					.radius_min = radius_min,
					.radius_max = radius_max,
					.cb = cb,
					.cb_data = cb_data,
				};
	v3f_t			aabb_min = v3f_init(-1.0f, -1.0f, -1.0f);
	v3f_t			aabb_max = v3f_init(1.0f, 1.0f, 1.0f);

	_bsp_search_sphere(bsp, &bsp->root, &search, &aabb_min, &aabb_max);
}
