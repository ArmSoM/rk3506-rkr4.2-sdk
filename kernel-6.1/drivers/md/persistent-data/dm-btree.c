/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm-btree-internal.h"
#include "dm-space-map.h"
#include "dm-transaction-manager.h"

#include <linux/export.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "btree"

/*----------------------------------------------------------------
 * Array manipulation
 *--------------------------------------------------------------*/
static void memcpy_disk(void *dest, const void *src, size_t len)
	__dm_written_to_disk(src)
{
	memcpy(dest, src, len);
	__dm_unbless_for_disk(src);
}

static void array_insert(void *base, size_t elt_size, unsigned int nr_elts,
			 unsigned int index, void *elt)
	__dm_written_to_disk(elt)
{
	if (index < nr_elts)
		memmove(base + (elt_size * (index + 1)),
			base + (elt_size * index),
			(nr_elts - index) * elt_size);

	memcpy_disk(base + (elt_size * index), elt, elt_size);
}

/*----------------------------------------------------------------*/

/* makes the assumption that no two keys are the same. */
static int bsearch(struct btree_node *n, uint64_t key, int want_hi)
{
	int lo = -1, hi = le32_to_cpu(n->header.nr_entries);

	while (hi - lo > 1) {
		int mid = lo + ((hi - lo) / 2);
		uint64_t mid_key = le64_to_cpu(n->keys[mid]);

		if (mid_key == key)
			return mid;

		if (mid_key < key)
			lo = mid;
		else
			hi = mid;
	}

	return want_hi ? hi : lo;
}

int lower_bound(struct btree_node *n, uint64_t key)
{
	return bsearch(n, key, 0);
}

static int upper_bound(struct btree_node *n, uint64_t key)
{
	return bsearch(n, key, 1);
}

void inc_children(struct dm_transaction_manager *tm, struct btree_node *n,
		  struct dm_btree_value_type *vt)
{
	uint32_t nr_entries = le32_to_cpu(n->header.nr_entries);

	if (le32_to_cpu(n->header.flags) & INTERNAL_NODE)
		dm_tm_with_runs(tm, value_ptr(n, 0), nr_entries, dm_tm_inc_range);

	else if (vt->inc)
		vt->inc(vt->context, value_ptr(n, 0), nr_entries);
}

static int insert_at(size_t value_size, struct btree_node *node, unsigned int index,
		     uint64_t key, void *value)
	__dm_written_to_disk(value)
{
	uint32_t nr_entries = le32_to_cpu(node->header.nr_entries);
	uint32_t max_entries = le32_to_cpu(node->header.max_entries);
	__le64 key_le = cpu_to_le64(key);

	if (index > nr_entries ||
	    index >= max_entries ||
	    nr_entries >= max_entries) {
		DMERR("too many entries in btree node for insert");
		__dm_unbless_for_disk(value);
		return -ENOMEM;
	}

	__dm_bless_for_disk(&key_le);

	array_insert(node->keys, sizeof(*node->keys), nr_entries, index, &key_le);
	array_insert(value_base(node), value_size, nr_entries, index, value);
	node->header.nr_entries = cpu_to_le32(nr_entries + 1);

	return 0;
}

/*----------------------------------------------------------------*/

/*
 * We want 3n entries (for some n).  This works more nicely for repeated
 * insert remove loops than (2n + 1).
 */
static uint32_t calc_max_entries(size_t value_size, size_t block_size)
{
	uint32_t total, n;
	size_t elt_size = sizeof(uint64_t) + value_size; /* key + value */

	block_size -= sizeof(struct node_header);
	total = block_size / elt_size;
	n = total / 3;		/* rounds down */

	return 3 * n;
}

int dm_btree_empty(struct dm_btree_info *info, dm_block_t *root)
{
	int r;
	struct dm_block *b;
	struct btree_node *n;
	size_t block_size;
	uint32_t max_entries;

	r = new_block(info, &b);
	if (r < 0)
		return r;

	block_size = dm_bm_block_size(dm_tm_get_bm(info->tm));
	max_entries = calc_max_entries(info->value_type.size, block_size);

	n = dm_block_data(b);
	memset(n, 0, block_size);
	n->header.flags = cpu_to_le32(LEAF_NODE);
	n->header.nr_entries = cpu_to_le32(0);
	n->header.max_entries = cpu_to_le32(max_entries);
	n->header.value_size = cpu_to_le32(info->value_type.size);

	*root = dm_block_location(b);
	unlock_block(info, b);

	return 0;
}
EXPORT_SYMBOL_GPL(dm_btree_empty);

/*----------------------------------------------------------------*/

/*
 * Deletion uses a recursive algorithm, since we have limited stack space
 * we explicitly manage our own stack on the heap.
 */
#define MAX_SPINE_DEPTH 64
struct frame {
	struct dm_block *b;
	struct btree_node *n;
	unsigned int level;
	unsigned int nr_children;
	unsigned int current_child;
};

struct del_stack {
	struct dm_btree_info *info;
	struct dm_transaction_manager *tm;
	int top;
	struct frame spine[MAX_SPINE_DEPTH];
};

static int top_frame(struct del_stack *s, struct frame **f)
{
	if (s->top < 0) {
		DMERR("btree deletion stack empty");
		return -EINVAL;
	}

	*f = s->spine + s->top;

	return 0;
}

static int unprocessed_frames(struct del_stack *s)
{
	return s->top >= 0;
}

static void prefetch_children(struct del_stack *s, struct frame *f)
{
	unsigned int i;
	struct dm_block_manager *bm = dm_tm_get_bm(s->tm);

	for (i = 0; i < f->nr_children; i++)
		dm_bm_prefetch(bm, value64(f->n, i));
}

static bool is_internal_level(struct dm_btree_info *info, struct frame *f)
{
	return f->level < (info->levels - 1);
}

static int push_frame(struct del_stack *s, dm_block_t b, unsigned int level)
{
	int r;
	uint32_t ref_count;

	if (s->top >= MAX_SPINE_DEPTH - 1) {
		DMERR("btree deletion stack out of memory");
		return -ENOMEM;
	}

	r = dm_tm_ref(s->tm, b, &ref_count);
	if (r)
		return r;

	if (ref_count > 1)
		/*
		 * This is a shared node, so we can just decrement it's
		 * reference counter and leave the children.
		 */
		dm_tm_dec(s->tm, b);

	else {
		uint32_t flags;
		struct frame *f = s->spine + ++s->top;

		r = dm_tm_read_lock(s->tm, b, &btree_node_validator, &f->b);
		if (r) {
			s->top--;
			return r;
		}

		f->n = dm_block_data(f->b);
		f->level = level;
		f->nr_children = le32_to_cpu(f->n->header.nr_entries);
		f->current_child = 0;

		flags = le32_to_cpu(f->n->header.flags);
		if (flags & INTERNAL_NODE || is_internal_level(s->info, f))
			prefetch_children(s, f);
	}

	return 0;
}

static void pop_frame(struct del_stack *s)
{
	struct frame *f = s->spine + s->top--;

	dm_tm_dec(s->tm, dm_block_location(f->b));
	dm_tm_unlock(s->tm, f->b);
}

static void unlock_all_frames(struct del_stack *s)
{
	struct frame *f;

	while (unprocessed_frames(s)) {
		f = s->spine + s->top--;
		dm_tm_unlock(s->tm, f->b);
	}
}

int dm_btree_del(struct dm_btree_info *info, dm_block_t root)
{
	int r;
	struct del_stack *s;

	/*
	 * dm_btree_del() is called via an ioctl, as such should be
	 * considered an FS op.  We can't recurse back into the FS, so we
	 * allocate GFP_NOFS.
	 */
	s = kmalloc(sizeof(*s), GFP_NOFS);
	if (!s)
		return -ENOMEM;
	s->info = info;
	s->tm = info->tm;
	s->top = -1;

	r = push_frame(s, root, 0);
	if (r)
		goto out;

	while (unprocessed_frames(s)) {
		uint32_t flags;
		struct frame *f;
		dm_block_t b;

		r = top_frame(s, &f);
		if (r)
			goto out;

		if (f->current_child >= f->nr_children) {
			pop_frame(s);
			continue;
		}

		flags = le32_to_cpu(f->n->header.flags);
		if (flags & INTERNAL_NODE) {
			b = value64(f->n, f->current_child);
			f->current_child++;
			r = push_frame(s, b, f->level);
			if (r)
				goto out;

		} else if (is_internal_level(info, f)) {
			b = value64(f->n, f->current_child);
			f->current_child++;
			r = push_frame(s, b, f->level + 1);
			if (r)
				goto out;

		} else {
			if (info->value_type.dec)
				info->value_type.dec(info->value_type.context,
						     value_ptr(f->n, 0), f->nr_children);
			pop_frame(s);
		}
	}
out:
	if (r) {
		/* cleanup all frames of del_stack */
		unlock_all_frames(s);
	}
	kfree(s);

	return r;
}
EXPORT_SYMBOL_GPL(dm_btree_del);

/*----------------------------------------------------------------*/

static int btree_lookup_raw(struct ro_spine *s, dm_block_t block, uint64_t key,
			    int (*search_fn)(struct btree_node *, uint64_t),
			    uint64_t *result_key, void *v, size_t value_size)
{
	int i, r;
	uint32_t flags, nr_entries;

	do {
		r = ro_step(s, block);
		if (r < 0)
			return r;

		i = search_fn(ro_node(s), key);

		flags = le32_to_cpu(ro_node(s)->header.flags);
		nr_entries = le32_to_cpu(ro_node(s)->header.nr_entries);
		if (i < 0 || i >= nr_entries)
			return -ENODATA;

		if (flags & INTERNAL_NODE)
			block = value64(ro_node(s), i);

	} while (!(flags & LEAF_NODE));

	*result_key = le64_to_cpu(ro_node(s)->keys[i]);
	if (v)
		memcpy(v, value_ptr(ro_node(s), i), value_size);

	return 0;
}

int dm_btree_lookup(struct dm_btree_info *info, dm_block_t root,
		    uint64_t *keys, void *value_le)
{
	unsigned int level, last_level = info->levels - 1;
	int r = -ENODATA;
	uint64_t rkey;
	__le64 internal_value_le;
	struct ro_spine spine;

	init_ro_spine(&spine, info);
	for (level = 0; level < info->levels; level++) {
		size_t size;
		void *value_p;

		if (level == last_level) {
			value_p = value_le;
			size = info->value_type.size;

		} else {
			value_p = &internal_value_le;
			size = sizeof(uint64_t);
		}

		r = btree_lookup_raw(&spine, root, keys[level],
				     lower_bound, &rkey,
				     value_p, size);

		if (!r) {
			if (rkey != keys[level]) {
				exit_ro_spine(&spine);
				return -ENODATA;
			}
		} else {
			exit_ro_spine(&spine);
			return r;
		}

		root = le64_to_cpu(internal_value_le);
	}
	exit_ro_spine(&spine);

	return r;
}
EXPORT_SYMBOL_GPL(dm_btree_lookup);

static int dm_btree_lookup_next_single(struct dm_btree_info *info, dm_block_t root,
				       uint64_t key, uint64_t *rkey, void *value_le)
{
	int r, i;
	uint32_t flags, nr_entries;
	struct dm_block *node;
	struct btree_node *n;

	r = bn_read_lock(info, root, &node);
	if (r)
		return r;

	n = dm_block_data(node);
	flags = le32_to_cpu(n->header.flags);
	nr_entries = le32_to_cpu(n->header.nr_entries);

	if (flags & INTERNAL_NODE) {
		i = lower_bound(n, key);
		if (i < 0) {
			/*
			 * avoid early -ENODATA return when all entries are
			 * higher than the search @key.
			 */
			i = 0;
		}
		if (i >= nr_entries) {
			r = -ENODATA;
			goto out;
		}

		r = dm_btree_lookup_next_single(info, value64(n, i), key, rkey, value_le);
		if (r == -ENODATA && i < (nr_entries - 1)) {
			i++;
			r = dm_btree_lookup_next_single(info, value64(n, i), key, rkey, value_le);
		}

	} else {
		i = upper_bound(n, key);
		if (i < 0 || i >= nr_entries) {
			r = -ENODATA;
			goto out;
		}

		*rkey = le64_to_cpu(n->keys[i]);
		memcpy(value_le, value_ptr(n, i), info->value_type.size);
	}
out:
	dm_tm_unlock(info->tm, node);
	return r;
}

int dm_btree_lookup_next(struct dm_btree_info *info, dm_block_t root,
			 uint64_t *keys, uint64_t *rkey, void *value_le)
{
	unsigned int level;
	int r = -ENODATA;
	__le64 internal_value_le;
	struct ro_spine spine;

	init_ro_spine(&spine, info);
	for (level = 0; level < info->levels - 1u; level++) {
		r = btree_lookup_raw(&spine, root, keys[level],
				     lower_bound, rkey,
				     &internal_value_le, sizeof(uint64_t));
		if (r)
			goto out;

		if (*rkey != keys[level]) {
			r = -ENODATA;
			goto out;
		}

		root = le64_to_cpu(internal_value_le);
	}

	r = dm_btree_lookup_next_single(info, root, keys[level], rkey, value_le);
out:
	exit_ro_spine(&spine);
	return r;
}

EXPORT_SYMBOL_GPL(dm_btree_lookup_next);

/*----------------------------------------------------------------*/

/*
 * Copies entries from one region of a btree node to another.  The regions
 * must not overlap.
 */
static void copy_entries(struct btree_node *dest, unsigned int dest_offset,
			 struct btree_node *src, unsigned int src_offset,
			 unsigned int count)
{
	size_t value_size = le32_to_cpu(dest->header.value_size);
	memcpy(dest->keys + dest_offset, src->keys + src_offset, count * sizeof(uint64_t));
	memcpy(value_ptr(dest, dest_offset), value_ptr(src, src_offset), count * value_size);
}

/*
 * Moves entries from one region fo a btree node to another.  The regions
 * may overlap.
 */
static void move_entries(struct btree_node *dest, unsigned int dest_offset,
			 struct btree_node *src, unsigned int src_offset,
			 unsigned int count)
{
	size_t value_size = le32_to_cpu(dest->header.value_size);
	memmove(dest->keys + dest_offset, src->keys + src_offset, count * sizeof(uint64_t));
	memmove(value_ptr(dest, dest_offset), value_ptr(src, src_offset), count * value_size);
}

/*
 * Erases the first 'count' entries of a btree node, shifting following
 * entries down into their place.
 */
static void shift_down(struct btree_node *n, unsigned int count)
{
	move_entries(n, 0, n, count, le32_to_cpu(n->header.nr_entries) - count);
}

/*
 * Moves entries in a btree node up 'count' places, making space for
 * new entries at the start of the node.
 */
static void shift_up(struct btree_node *n, unsigned int count)
{
	move_entries(n, count, n, 0, le32_to_cpu(n->header.nr_entries));
}

/*
 * Redistributes entries between two btree nodes to make them
 * have similar numbers of entries.
 */
static void redistribute2(struct btree_node *left, struct btree_node *right)
{
	unsigned int nr_left = le32_to_cpu(left->header.nr_entries);
	unsigned int nr_right = le32_to_cpu(right->header.nr_entries);
	unsigned int total = nr_left + nr_right;
	unsigned int target_left = total / 2;
	unsigned int target_right = total - target_left;

	if (nr_left < target_left) {
		unsigned int delta = target_left - nr_left;
		copy_entries(left, nr_left, right, 0, delta);
		shift_down(right, delta);
	} else if (nr_left > target_left) {
		unsigned int delta = nr_left - target_left;
		if (nr_right)
			shift_up(right, delta);
		copy_entries(right, 0, left, target_left, delta);
	}

	left->header.nr_entries = cpu_to_le32(target_left);
	right->header.nr_entries = cpu_to_le32(target_right);
}

/*
 * Redistribute entries between three nodes.  Assumes the central
 * node is empty.
 */
static void redistribute3(struct btree_node *left, struct btree_node *center,
			  struct btree_node *right)
{
	unsigned int nr_left = le32_to_cpu(left->header.nr_entries);
	unsigned int nr_center = le32_to_cpu(center->header.nr_entries);
	unsigned int nr_right = le32_to_cpu(right->header.nr_entries);
	unsigned int total, target_left, target_center, target_right;

	BUG_ON(nr_center);

	total = nr_left + nr_right;
	target_left = total / 3;
	target_center = (total - target_left) / 2;
	target_right = (total - target_left - target_center);

	if (nr_left < target_left) {
		unsigned int left_short = target_left - nr_left;
		copy_entries(left, nr_left, right, 0, left_short);
		copy_entries(center, 0, right, left_short, target_center);
		shift_down(right, nr_right - target_right);

	} else if (nr_left < (target_left + target_center)) {
		unsigned int left_to_center = nr_left - target_left;
		copy_entries(center, 0, left, target_left, left_to_center);
		copy_entries(center, left_to_center, right, 0, target_center - left_to_center);
		shift_down(right, nr_right - target_right);

	} else {
		unsigned int right_short = target_right - nr_right;
		shift_up(right, right_short);
		copy_entries(right, 0, left, nr_left - right_short, right_short);
		copy_entries(center, 0, left, target_left, nr_left - target_left);
	}

	left->header.nr_entries = cpu_to_le32(target_left);
	center->header.nr_entries = cpu_to_le32(target_center);
	right->header.nr_entries = cpu_to_le32(target_right);
}

/*
 * Splits a node by creating a sibling node and shifting half the nodes
 * contents across.  Assumes there is a parent node, and it has room for
 * another child.
 *
 * Before:
 *	  +--------+
 *	  | Parent |
 *	  +--------+
 *	     |
 *	     v
 *	+----------+
 *	| A ++++++ |
 *	+----------+
 *
 *
 * After:
 *		+--------+
 *		| Parent |
 *		+--------+
 *		  |	|
 *		  v	+------+
 *	    +---------+	       |
 *	    | A* +++  |	       v
 *	    +---------+	  +-------+
 *			  | B +++ |
 *			  +-------+
 *
 * Where A* is a shadow of A.
 */
static int split_one_into_two(struct shadow_spine *s, unsigned int parent_index,
			      struct dm_btree_value_type *vt, uint64_t key)
{
	int r;
	struct dm_block *left, *right, *parent;
	struct btree_node *ln, *rn, *pn;
	__le64 location;

	left = shadow_current(s);

	r = new_block(s->info, &right);
	if (r < 0)
		return r;

	ln = dm_block_data(left);
	rn = dm_block_data(right);

	rn->header.flags = ln->header.flags;
	rn->header.nr_entries = cpu_to_le32(0);
	rn->header.max_entries = ln->header.max_entries;
	rn->header.value_size = ln->header.value_size;
	redistribute2(ln, rn);

	/* patch up the parent */
	parent = shadow_parent(s);
	pn = dm_block_data(parent);

	location = cpu_to_le64(dm_block_location(right));
	__dm_bless_for_disk(&location);
	r = insert_at(sizeof(__le64), pn, parent_index + 1,
		      le64_to_cpu(rn->keys[0]), &location);
	if (r) {
		unlock_block(s->info, right);
		return r;
	}

	/* patch up the spine */
	if (key < le64_to_cpu(rn->keys[0])) {
		unlock_block(s->info, right);
		s->nodes[1] = left;
	} else {
		unlock_block(s->info, left);
		s->nodes[1] = right;
	}

	return 0;
}

/*
 * We often need to modify a sibling node.  This function shadows a particular
 * child of the given parent node.  Making sure to update the parent to point
 * to the new shadow.
 */
static int shadow_child(struct dm_btree_info *info, struct dm_btree_value_type *vt,
			struct btree_node *parent, unsigned int index,
			struct dm_block **result)
{
	int r, inc;
	dm_block_t root;
	struct btree_node *node;

	root = value64(parent, index);

	r = dm_tm_shadow_block(info->tm, root, &btree_node_validator,
			       result, &inc);
	if (r)
		return r;

	node = dm_block_data(*result);

	if (inc)
		inc_children(info->tm, node, vt);

	*((__le64 *) value_ptr(parent, index)) =
		cpu_to_le64(dm_block_location(*result));

	return 0;
}

/*
 * Splits two nodes into three.  This is more work, but results in fuller
 * nodes, so saves metadata space.
 */
static int split_two_into_three(struct shadow_spine *s, unsigned int parent_index,
				struct dm_btree_value_type *vt, uint64_t key)
{
	int r;
	unsigned int middle_index;
	struct dm_block *left, *middle, *right, *parent;
	struct btree_node *ln, *rn, *mn, *pn;
	__le64 location;

	parent = shadow_parent(s);
	pn = dm_block_data(parent);

	if (parent_index == 0) {
		middle_index = 1;
		left = shadow_current(s);
		r = shadow_child(s->info, vt, pn, parent_index + 1, &right);
		if (r)
			return r;
	} else {
		middle_index = parent_index;
		right = shadow_current(s);
		r = shadow_child(s->info, vt, pn, parent_index - 1, &left);
		if (r)
			return r;
	}

	r = new_block(s->info, &middle);
	if (r < 0)
		return r;

	ln = dm_block_data(left);
	mn = dm_block_data(middle);
	rn = dm_block_data(right);

	mn->header.nr_entries = cpu_to_le32(0);
	mn->header.flags = ln->header.flags;
	mn->header.max_entries = ln->header.max_entries;
	mn->header.value_size = ln->header.value_size;

	redistribute3(ln, mn, rn);

	/* patch up the parent */
	pn->keys[middle_index] = rn->keys[0];
	location = cpu_to_le64(dm_block_location(middle));
	__dm_bless_for_disk(&location);
	r = insert_at(sizeof(__le64), pn, middle_index,
		      le64_to_cpu(mn->keys[0]), &location);
	if (r) {
		if (shadow_current(s) != left)
			unlock_block(s->info, left);

		unlock_block(s->info, middle);

		if (shadow_current(s) != right)
			unlock_block(s->info, right);

		return r;
	}


	/* patch up the spine */
	if (key < le64_to_cpu(mn->keys[0])) {
		unlock_block(s->info, middle);
		unlock_block(s->info, right);
		s->nodes[1] = left;
	} else if (key < le64_to_cpu(rn->keys[0])) {
		unlock_block(s->info, left);
		unlock_block(s->info, right);
		s->nodes[1] = middle;
	} else {
		unlock_block(s->info, left);
		unlock_block(s->info, middle);
		s->nodes[1] = right;
	}

	return 0;
}

/*----------------------------------------------------------------*/

/*
 * Splits a node by creating two new children beneath the given node.
 *
 * Before:
 *	  +----------+
 *	  | A ++++++ |
 *	  +----------+
 *
 *
 * After:
 *	+------------+
 *	| A (shadow) |
 *	+------------+
 *	    |	|
 *   +------+	+----+
 *   |		     |
 *   v		     v
 * +-------+	 +-------+
 * | B +++ |	 | C +++ |
 * +-------+	 +-------+
 */
static int btree_split_beneath(struct shadow_spine *s, uint64_t key)
{
	int r;
	size_t size;
	unsigned int nr_left, nr_right;
	struct dm_block *left, *right, *new_parent;
	struct btree_node *pn, *ln, *rn;
	__le64 val;

	new_parent = shadow_current(s);

	pn = dm_block_data(new_parent);
	size = le32_to_cpu(pn->header.flags) & INTERNAL_NODE ?
		sizeof(__le64) : s->info->value_type.size;

	/* create & init the left block */
	r = new_block(s->info, &left);
	if (r < 0)
		return r;

	ln = dm_block_data(left);
	nr_left = le32_to_cpu(pn->header.nr_entries) / 2;

	ln->header.flags = pn->header.flags;
	ln->header.nr_entries = cpu_to_le32(nr_left);
	ln->header.max_entries = pn->header.max_entries;
	ln->header.value_size = pn->header.value_size;
	memcpy(ln->keys, pn->keys, nr_left * sizeof(pn->keys[0]));
	memcpy(value_ptr(ln, 0), value_ptr(pn, 0), nr_left * size);

	/* create & init the right block */
	r = new_block(s->info, &right);
	if (r < 0) {
		unlock_block(s->info, left);
		return r;
	}

	rn = dm_block_data(right);
	nr_right = le32_to_cpu(pn->header.nr_entries) - nr_left;

	rn->header.flags = pn->header.flags;
	rn->header.nr_entries = cpu_to_le32(nr_right);
	rn->header.max_entries = pn->header.max_entries;
	rn->header.value_size = pn->header.value_size;
	memcpy(rn->keys, pn->keys + nr_left, nr_right * sizeof(pn->keys[0]));
	memcpy(value_ptr(rn, 0), value_ptr(pn, nr_left),
	       nr_right * size);

	/* new_parent should just point to l and r now */
	pn->header.flags = cpu_to_le32(INTERNAL_NODE);
	pn->header.nr_entries = cpu_to_le32(2);
	pn->header.max_entries = cpu_to_le32(
		calc_max_entries(sizeof(__le64),
				 dm_bm_block_size(
					 dm_tm_get_bm(s->info->tm))));
	pn->header.value_size = cpu_to_le32(sizeof(__le64));

	val = cpu_to_le64(dm_block_location(left));
	__dm_bless_for_disk(&val);
	pn->keys[0] = ln->keys[0];
	memcpy_disk(value_ptr(pn, 0), &val, sizeof(__le64));

	val = cpu_to_le64(dm_block_location(right));
	__dm_bless_for_disk(&val);
	pn->keys[1] = rn->keys[0];
	memcpy_disk(value_ptr(pn, 1), &val, sizeof(__le64));

	unlock_block(s->info, left);
	unlock_block(s->info, right);
	return 0;
}

/*----------------------------------------------------------------*/

/*
 * Redistributes a node's entries with its left sibling.
 */
static int rebalance_left(struct shadow_spine *s, struct dm_btree_value_type *vt,
			  unsigned int parent_index, uint64_t key)
{
	int r;
	struct dm_block *sib;
	struct btree_node *left, *right, *parent = dm_block_data(shadow_parent(s));

	r = shadow_child(s->info, vt, parent, parent_index - 1, &sib);
	if (r)
		return r;

	left = dm_block_data(sib);
	right = dm_block_data(shadow_current(s));
	redistribute2(left, right);
	*key_ptr(parent, parent_index) = right->keys[0];

	if (key < le64_to_cpu(right->keys[0])) {
		unlock_block(s->info, s->nodes[1]);
		s->nodes[1] = sib;
	} else {
		unlock_block(s->info, sib);
	}

	return 0;
}

/*
 * Redistributes a nodes entries with its right sibling.
 */
static int rebalance_right(struct shadow_spine *s, struct dm_btree_value_type *vt,
			   unsigned int parent_index, uint64_t key)
{
	int r;
	struct dm_block *sib;
	struct btree_node *left, *right, *parent = dm_block_data(shadow_parent(s));

	r = shadow_child(s->info, vt, parent, parent_index + 1, &sib);
	if (r)
		return r;

	left = dm_block_data(shadow_current(s));
	right = dm_block_data(sib);
	redistribute2(left, right);
	*key_ptr(parent, parent_index + 1) = right->keys[0];

	if (key < le64_to_cpu(right->keys[0])) {
		unlock_block(s->info, sib);
	} else {
		unlock_block(s->info, s->nodes[1]);
		s->nodes[1] = sib;
	}

	return 0;
}

/*
 * Returns the number of spare entries in a node.
 */
static int get_node_free_space(struct dm_btree_info *info, dm_block_t b, unsigned int *space)
{
	int r;
	unsigned int nr_entries;
	struct dm_block *block;
	struct btree_node *node;

	r = bn_read_lock(info, b, &block);
	if (r)
		return r;

	node = dm_block_data(block);
	nr_entries = le32_to_cpu(node->header.nr_entries);
	*space = le32_to_cpu(node->header.max_entries) - nr_entries;

	unlock_block(info, block);
	return 0;
}

/*
 * Make space in a node, either by moving some entries to a sibling,
 * or creating a new sibling node.  SPACE_THRESHOLD defines the minimum
 * number of free entries that must be in the sibling to make the move
 * worth while.  If the siblings are shared (eg, part of a snapshot),
 * then they are not touched, since this break sharing and so consume
 * more space than we save.
 */
#define SPACE_THRESHOLD 8
static int rebalance_or_split(struct shadow_spine *s, struct dm_btree_value_type *vt,
			      unsigned int parent_index, uint64_t key)
{
	int r;
	struct btree_node *parent = dm_block_data(shadow_parent(s));
	unsigned int nr_parent = le32_to_cpu(parent->header.nr_entries);
	unsigned int free_space;
	int left_shared = 0, right_shared = 0;

	/* Should we move entries to the left sibling? */
	if (parent_index > 0) {
		dm_block_t left_b = value64(parent, parent_index - 1);
		r = dm_tm_block_is_shared(s->info->tm, left_b, &left_shared);
		if (r)
			return r;

		if (!left_shared) {
			r = get_node_free_space(s->info, left_b, &free_space);
			if (r)
				return r;

			if (free_space >= SPACE_THRESHOLD)
				return rebalance_left(s, vt, parent_index, key);
		}
	}

	/* Should we move entries to the right sibling? */
	if (parent_index < (nr_parent - 1)) {
		dm_block_t right_b = value64(parent, parent_index + 1);
		r = dm_tm_block_is_shared(s->info->tm, right_b, &right_shared);
		if (r)
			return r;

		if (!right_shared) {
			r = get_node_free_space(s->info, right_b, &free_space);
			if (r)
				return r;

			if (free_space >= SPACE_THRESHOLD)
				return rebalance_right(s, vt, parent_index, key);
		}
	}

	/*
	 * We need to split the node, normally we split two nodes
	 * into three.	But when inserting a sequence that is either
	 * monotonically increasing or decreasing it's better to split
	 * a single node into two.
	 */
	if (left_shared || right_shared || (nr_parent <= 2) ||
	    (parent_index == 0) || (parent_index + 1 == nr_parent)) {
		return split_one_into_two(s, parent_index, vt, key);
	} else {
		return split_two_into_three(s, parent_index, vt, key);
	}
}

/*
 * Does the node contain a particular key?
 */
static bool contains_key(struct btree_node *node, uint64_t key)
{
	int i = lower_bound(node, key);

	if (i >= 0 && le64_to_cpu(node->keys[i]) == key)
		return true;

	return false;
}

/*
 * In general we preemptively make sure there's a free entry in every
 * node on the spine when doing an insert.  But we can avoid that with
 * leaf nodes if we know it's an overwrite.
 */
static bool has_space_for_insert(struct btree_node *node, uint64_t key)
{
	if (node->header.nr_entries == node->header.max_entries) {
		if (le32_to_cpu(node->header.flags) & LEAF_NODE) {
			/* we don't need space if it's an overwrite */
			return contains_key(node, key);
		}

		return false;
	}

	return true;
}

static int btree_insert_raw(struct shadow_spine *s, dm_block_t root,
			    struct dm_btree_value_type *vt,
			    uint64_t key, unsigned int *index)
{
	int r, i = *index, top = 1;
	struct btree_node *node;

	for (;;) {
		r = shadow_step(s, root, vt);
		if (r < 0)
			return r;

		node = dm_block_data(shadow_current(s));

		/*
		 * We have to patch up the parent node, ugly, but I don't
		 * see a way to do this automatically as part of the spine
		 * op.
		 */
		if (shadow_has_parent(s) && i >= 0) { /* FIXME: second clause unness. */
			__le64 location = cpu_to_le64(dm_block_location(shadow_current(s)));

			__dm_bless_for_disk(&location);
			memcpy_disk(value_ptr(dm_block_data(shadow_parent(s)), i),
				    &location, sizeof(__le64));
		}

		node = dm_block_data(shadow_current(s));

		if (!has_space_for_insert(node, key)) {
			if (top)
				r = btree_split_beneath(s, key);
			else
				r = rebalance_or_split(s, vt, i, key);

			if (r < 0)
				return r;

			/* making space can cause the current node to change */
			node = dm_block_data(shadow_current(s));
		}

		i = lower_bound(node, key);

		if (le32_to_cpu(node->header.flags) & LEAF_NODE)
			break;

		if (i < 0) {
			/* change the bounds on the lowest key */
			node->keys[0] = cpu_to_le64(key);
			i = 0;
		}

		root = value64(node, i);
		top = 0;
	}

	if (i < 0 || le64_to_cpu(node->keys[i]) != key)
		i++;

	*index = i;
	return 0;
}

static int __btree_get_overwrite_leaf(struct shadow_spine *s, dm_block_t root,
				      uint64_t key, int *index)
{
	int r, i = -1;
	struct btree_node *node;

	*index = 0;
	for (;;) {
		r = shadow_step(s, root, &s->info->value_type);
		if (r < 0)
			return r;

		node = dm_block_data(shadow_current(s));

		/*
		 * We have to patch up the parent node, ugly, but I don't
		 * see a way to do this automatically as part of the spine
		 * op.
		 */
		if (shadow_has_parent(s) && i >= 0) {
			__le64 location = cpu_to_le64(dm_block_location(shadow_current(s)));

			__dm_bless_for_disk(&location);
			memcpy_disk(value_ptr(dm_block_data(shadow_parent(s)), i),
				    &location, sizeof(__le64));
		}

		node = dm_block_data(shadow_current(s));
		i = lower_bound(node, key);

		BUG_ON(i < 0);
		BUG_ON(i >= le32_to_cpu(node->header.nr_entries));

		if (le32_to_cpu(node->header.flags) & LEAF_NODE) {
			if (key != le64_to_cpu(node->keys[i]))
				return -EINVAL;
			break;
		}

		root = value64(node, i);
	}

	*index = i;
	return 0;
}

int btree_get_overwrite_leaf(struct dm_btree_info *info, dm_block_t root,
			     uint64_t key, int *index,
			     dm_block_t *new_root, struct dm_block **leaf)
{
	int r;
	struct shadow_spine spine;

	BUG_ON(info->levels > 1);
	init_shadow_spine(&spine, info);
	r = __btree_get_overwrite_leaf(&spine, root, key, index);
	if (!r) {
		*new_root = shadow_root(&spine);
		*leaf = shadow_current(&spine);

		/*
		 * Decrement the count so exit_shadow_spine() doesn't
		 * unlock the leaf.
		 */
		spine.count--;
	}
	exit_shadow_spine(&spine);

	return r;
}

static bool need_insert(struct btree_node *node, uint64_t *keys,
			unsigned int level, unsigned int index)
{
	return ((index >= le32_to_cpu(node->header.nr_entries)) ||
		(le64_to_cpu(node->keys[index]) != keys[level]));
}

static int insert(struct dm_btree_info *info, dm_block_t root,
		  uint64_t *keys, void *value, dm_block_t *new_root,
		  int *inserted)
		  __dm_written_to_disk(value)
{
	int r;
	unsigned int level, index = -1, last_level = info->levels - 1;
	dm_block_t block = root;
	struct shadow_spine spine;
	struct btree_node *n;
	struct dm_btree_value_type le64_type;

	init_le64_type(info->tm, &le64_type);
	init_shadow_spine(&spine, info);

	for (level = 0; level < (info->levels - 1); level++) {
		r = btree_insert_raw(&spine, block, &le64_type, keys[level], &index);
		if (r < 0)
			goto bad;

		n = dm_block_data(shadow_current(&spine));

		if (need_insert(n, keys, level, index)) {
			dm_block_t new_tree;
			__le64 new_le;

			r = dm_btree_empty(info, &new_tree);
			if (r < 0)
				goto bad;

			new_le = cpu_to_le64(new_tree);
			__dm_bless_for_disk(&new_le);

			r = insert_at(sizeof(uint64_t), n, index,
				      keys[level], &new_le);
			if (r)
				goto bad;
		}

		if (level < last_level)
			block = value64(n, index);
	}

	r = btree_insert_raw(&spine, block, &info->value_type,
			     keys[level], &index);
	if (r < 0)
		goto bad;

	n = dm_block_data(shadow_current(&spine));

	if (need_insert(n, keys, level, index)) {
		if (inserted)
			*inserted = 1;

		r = insert_at(info->value_type.size, n, index,
			      keys[level], value);
		if (r)
			goto bad_unblessed;
	} else {
		if (inserted)
			*inserted = 0;

		if (info->value_type.dec &&
		    (!info->value_type.equal ||
		     !info->value_type.equal(
			     info->value_type.context,
			     value_ptr(n, index),
			     value))) {
			info->value_type.dec(info->value_type.context,
					     value_ptr(n, index), 1);
		}
		memcpy_disk(value_ptr(n, index),
			    value, info->value_type.size);
	}

	*new_root = shadow_root(&spine);
	exit_shadow_spine(&spine);

	return 0;

bad:
	__dm_unbless_for_disk(value);
bad_unblessed:
	exit_shadow_spine(&spine);
	return r;
}

int dm_btree_insert(struct dm_btree_info *info, dm_block_t root,
		    uint64_t *keys, void *value, dm_block_t *new_root)
		    __dm_written_to_disk(value)
{
	return insert(info, root, keys, value, new_root, NULL);
}
EXPORT_SYMBOL_GPL(dm_btree_insert);

int dm_btree_insert_notify(struct dm_btree_info *info, dm_block_t root,
			   uint64_t *keys, void *value, dm_block_t *new_root,
			   int *inserted)
			   __dm_written_to_disk(value)
{
	return insert(info, root, keys, value, new_root, inserted);
}
EXPORT_SYMBOL_GPL(dm_btree_insert_notify);

/*----------------------------------------------------------------*/

static int find_key(struct ro_spine *s, dm_block_t block, bool find_highest,
		    uint64_t *result_key, dm_block_t *next_block)
{
	int i, r;
	uint32_t flags;

	do {
		r = ro_step(s, block);
		if (r < 0)
			return r;

		flags = le32_to_cpu(ro_node(s)->header.flags);
		i = le32_to_cpu(ro_node(s)->header.nr_entries);
		if (!i)
			return -ENODATA;
		else
			i--;

		if (find_highest)
			*result_key = le64_to_cpu(ro_node(s)->keys[i]);
		else
			*result_key = le64_to_cpu(ro_node(s)->keys[0]);

		if (next_block || flags & INTERNAL_NODE) {
			if (find_highest)
				block = value64(ro_node(s), i);
			else
				block = value64(ro_node(s), 0);
		}

	} while (flags & INTERNAL_NODE);

	if (next_block)
		*next_block = block;
	return 0;
}

static int dm_btree_find_key(struct dm_btree_info *info, dm_block_t root,
			     bool find_highest, uint64_t *result_keys)
{
	int r = 0, count = 0, level;
	struct ro_spine spine;

	init_ro_spine(&spine, info);
	for (level = 0; level < info->levels; level++) {
		r = find_key(&spine, root, find_highest, result_keys + level,
			     level == info->levels - 1 ? NULL : &root);
		if (r == -ENODATA) {
			r = 0;
			break;

		} else if (r)
			break;

		count++;
	}
	exit_ro_spine(&spine);

	return r ? r : count;
}

int dm_btree_find_highest_key(struct dm_btree_info *info, dm_block_t root,
			      uint64_t *result_keys)
{
	return dm_btree_find_key(info, root, true, result_keys);
}
EXPORT_SYMBOL_GPL(dm_btree_find_highest_key);

int dm_btree_find_lowest_key(struct dm_btree_info *info, dm_block_t root,
			     uint64_t *result_keys)
{
	return dm_btree_find_key(info, root, false, result_keys);
}
EXPORT_SYMBOL_GPL(dm_btree_find_lowest_key);

/*----------------------------------------------------------------*/

/*
 * FIXME: We shouldn't use a recursive algorithm when we have limited stack
 * space.  Also this only works for single level trees.
 */
static int walk_node(struct dm_btree_info *info, dm_block_t block,
		     int (*fn)(void *context, uint64_t *keys, void *leaf),
		     void *context)
{
	int r;
	unsigned int i, nr;
	struct dm_block *node;
	struct btree_node *n;
	uint64_t keys;

	r = bn_read_lock(info, block, &node);
	if (r)
		return r;

	n = dm_block_data(node);

	nr = le32_to_cpu(n->header.nr_entries);
	for (i = 0; i < nr; i++) {
		if (le32_to_cpu(n->header.flags) & INTERNAL_NODE) {
			r = walk_node(info, value64(n, i), fn, context);
			if (r)
				goto out;
		} else {
			keys = le64_to_cpu(*key_ptr(n, i));
			r = fn(context, &keys, value_ptr(n, i));
			if (r)
				goto out;
		}
	}

out:
	dm_tm_unlock(info->tm, node);
	return r;
}

int dm_btree_walk(struct dm_btree_info *info, dm_block_t root,
		  int (*fn)(void *context, uint64_t *keys, void *leaf),
		  void *context)
{
	BUG_ON(info->levels > 1);
	return walk_node(info, root, fn, context);
}
EXPORT_SYMBOL_GPL(dm_btree_walk);

/*----------------------------------------------------------------*/

static void prefetch_values(struct dm_btree_cursor *c)
{
	unsigned int i, nr;
	__le64 value_le;
	struct cursor_node *n = c->nodes + c->depth - 1;
	struct btree_node *bn = dm_block_data(n->b);
	struct dm_block_manager *bm = dm_tm_get_bm(c->info->tm);

	BUG_ON(c->info->value_type.size != sizeof(value_le));

	nr = le32_to_cpu(bn->header.nr_entries);
	for (i = 0; i < nr; i++) {
		memcpy(&value_le, value_ptr(bn, i), sizeof(value_le));
		dm_bm_prefetch(bm, le64_to_cpu(value_le));
	}
}

static bool leaf_node(struct dm_btree_cursor *c)
{
	struct cursor_node *n = c->nodes + c->depth - 1;
	struct btree_node *bn = dm_block_data(n->b);

	return le32_to_cpu(bn->header.flags) & LEAF_NODE;
}

static int push_node(struct dm_btree_cursor *c, dm_block_t b)
{
	int r;
	struct cursor_node *n = c->nodes + c->depth;

	if (c->depth >= DM_BTREE_CURSOR_MAX_DEPTH - 1) {
		DMERR("couldn't push cursor node, stack depth too high");
		return -EINVAL;
	}

	r = bn_read_lock(c->info, b, &n->b);
	if (r)
		return r;

	n->index = 0;
	c->depth++;

	if (c->prefetch_leaves || !leaf_node(c))
		prefetch_values(c);

	return 0;
}

static void pop_node(struct dm_btree_cursor *c)
{
	c->depth--;
	unlock_block(c->info, c->nodes[c->depth].b);
}

static int inc_or_backtrack(struct dm_btree_cursor *c)
{
	struct cursor_node *n;
	struct btree_node *bn;

	for (;;) {
		if (!c->depth)
			return -ENODATA;

		n = c->nodes + c->depth - 1;
		bn = dm_block_data(n->b);

		n->index++;
		if (n->index < le32_to_cpu(bn->header.nr_entries))
			break;

		pop_node(c);
	}

	return 0;
}

static int find_leaf(struct dm_btree_cursor *c)
{
	int r = 0;
	struct cursor_node *n;
	struct btree_node *bn;
	__le64 value_le;

	for (;;) {
		n = c->nodes + c->depth - 1;
		bn = dm_block_data(n->b);

		if (le32_to_cpu(bn->header.flags) & LEAF_NODE)
			break;

		memcpy(&value_le, value_ptr(bn, n->index), sizeof(value_le));
		r = push_node(c, le64_to_cpu(value_le));
		if (r) {
			DMERR("push_node failed");
			break;
		}
	}

	if (!r && (le32_to_cpu(bn->header.nr_entries) == 0))
		return -ENODATA;

	return r;
}

int dm_btree_cursor_begin(struct dm_btree_info *info, dm_block_t root,
			  bool prefetch_leaves, struct dm_btree_cursor *c)
{
	int r;

	c->info = info;
	c->root = root;
	c->depth = 0;
	c->prefetch_leaves = prefetch_leaves;

	r = push_node(c, root);
	if (r)
		return r;

	return find_leaf(c);
}
EXPORT_SYMBOL_GPL(dm_btree_cursor_begin);

void dm_btree_cursor_end(struct dm_btree_cursor *c)
{
	while (c->depth)
		pop_node(c);
}
EXPORT_SYMBOL_GPL(dm_btree_cursor_end);

int dm_btree_cursor_next(struct dm_btree_cursor *c)
{
	int r = inc_or_backtrack(c);
	if (!r) {
		r = find_leaf(c);
		if (r)
			DMERR("find_leaf failed");
	}

	return r;
}
EXPORT_SYMBOL_GPL(dm_btree_cursor_next);

int dm_btree_cursor_skip(struct dm_btree_cursor *c, uint32_t count)
{
	int r = 0;

	while (count-- && !r)
		r = dm_btree_cursor_next(c);

	return r;
}
EXPORT_SYMBOL_GPL(dm_btree_cursor_skip);

int dm_btree_cursor_get_value(struct dm_btree_cursor *c, uint64_t *key, void *value_le)
{
	if (c->depth) {
		struct cursor_node *n = c->nodes + c->depth - 1;
		struct btree_node *bn = dm_block_data(n->b);

		if (le32_to_cpu(bn->header.flags) & INTERNAL_NODE)
			return -EINVAL;

		*key = le64_to_cpu(*key_ptr(bn, n->index));
		memcpy(value_le, value_ptr(bn, n->index), c->info->value_type.size);
		return 0;

	} else
		return -ENODATA;
}
EXPORT_SYMBOL_GPL(dm_btree_cursor_get_value);
