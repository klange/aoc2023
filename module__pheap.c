#include <assert.h>
#include <stdlib.h>
#include <kuroko/vm.h>
#include <kuroko/util.h>

static KrkClass * PHeapClass;

typedef struct PHeap PHeap;
struct PHeap {
	struct PHeap_Obj * owner;
	KrkValue value;
	PHeap * subheaps; /* If this has a heap, it has a list of children */
	PHeap * next;  /* If this heap has a sibling to the right, this is it */
};

typedef int (*pheap_comparator_func)(PHeap *, PHeap *);

/**
 * meld - Combine two heaps.
 *        Melding is an O(1) operation.
 */
static PHeap * pheap_meld(PHeap * left, PHeap * right, pheap_comparator_func comparator) {
	/*
	 * If either of the heaps is "empty" (represented by NULL),
	 * then simply return the other one.
	 */
	if (!left) {
		return right;
	}
	if (!right) {
		return left;
	}

	/*
	 * Otherwise, pull the 'smaller' of the two up and add the 'larger'
	 * to the front of the subheap list of the smaller one. We use
	 * intrusive lists within our Heap struct, so each Heap is also
	 * a List node (with a `next` pointer).
	 */
	if (comparator(left, right)) {
		/* Turns `left` into Heap(left→value, right :: left→subheaps) */
		if (left->subheaps) {
			right->next = left->subheaps;
		}
		left->subheaps = right;
		return left;
	} else {
		/* Turns `right` into Heap(right→value, left :: right→subheaps) */
		if (right->subheaps) {
			left->next = right->subheaps;
		}
		right->subheaps = left;
		return right;
	}
}

/**
 * insert - add a new element to an existing heap (which may be empty)
 *          Since inserting is just a meld, it is O(1)
 */
PHeap * pheap_insert(PHeap * heap, PHeap * elem, pheap_comparator_func comparator) {
	/*
	 * Inserting is a simple matter of making a new Heap(elem, []) and
	 * then melding it with the existing heap. We store Heap objects
	 * on the, uh, heap (system heap, that is).
	 */
	elem->subheaps = NULL;
	elem->next = NULL;
	return pheap_meld(elem, heap, comparator);
}

/**
 * merge_pairs - the core of the pairing heap, performs a left-to-right and
 *               then right-to-left merge of a list of subheaps.
 *               This is O(log n) as we must recursively meld to
 *               perform the two-stage merge process.
 */
static PHeap * pheap_merge_pairs(PHeap * list, pheap_comparator_func comparator) {
	if (!list) {
		/* An empty list is represented by NULL, and yields an empty Heap,
		 * which is also represented by NULL... */
		return NULL;
	} else if (list->next == NULL) {
		/* If a list entry doesn't have a next, it has a size of one,
		 * and we can just return this heap directly. */
		return list;
	} else {
		/* Otherwise we meld the first two, then meld them with the result of
		 * recursively melding the rest, which performs our left-right /
		 * right-left two-stage merge. */
		PHeap * next  = list->next;
		list->next = NULL;
		PHeap * rest = next->next;
		next->next = NULL;
		return pheap_meld(pheap_meld(list, next, comparator), pheap_merge_pairs(rest, comparator), comparator);
	}
}

/**
 * delete_min - Remove the 'smallest' value from the heap.
 *              As a fundamental property of a heap, this should always
 *              be the "root" node, and should never be part of a list
 *              of subheaps, so we just need to rebalance its own children.
 *              As deleting consists of a call to merge_pairs, it is O(log n)
 */
PHeap * pheap_delete_min(PHeap * heap, pheap_comparator_func comparator) {
	PHeap * subs = heap->subheaps;
	return pheap_merge_pairs(subs, comparator);
}

/**
 * visit_heap - Call a user function for ever node in the heap.
 */
void pheap_visit_heap(PHeap * heap, void (*func)(PHeap *, void*), void* extra) {
	if (!heap) return;
	func(heap, extra);
	pheap_visit_heap(heap->subheaps, func, extra);
	pheap_visit_heap(heap->next, func, extra);
}

void pheap_visit_heap_after(PHeap * heap, void (*func)(PHeap *, void*), void* extra) {
	if (!heap) return;
	pheap_visit_heap_after(heap->subheaps, func, extra);
	pheap_visit_heap_after(heap->next, func, extra);
	func(heap, extra);
}

struct PHeap_Obj {
	KrkInstance inst;
	KrkValue comparator;
	PHeap * heap;
	size_t count;
};

#define IS_PHeap(o) (krk_isInstanceOf(o,PHeapClass))
#define AS_PHeap(o) ((struct PHeap_Obj*)AS_OBJECT(o))
#define CURRENT_CTYPE struct PHeap_Obj *
#define CURRENT_NAME self

KRK_Method(PHeap,__init__) {
	KrkValue comparator;
	if (!krk_parseArgs(".V:PHeap", (const char*[]){"comp"}, &comparator)) return NONE_VAL();
	self->comparator = comparator;
	return NONE_VAL();
}

static int run_comparator(PHeap * left, PHeap * right) {
	assert(left->owner == right->owner);
	krk_push(left->owner->comparator);
	krk_push(left->value);
	krk_push(right->value);
	KrkValue result = krk_callStack(2);
	if (!IS_BOOLEAN(result)) return 0;
	return AS_BOOLEAN(result);
}

KRK_Method(PHeap,insert) {
	KrkValue value;
	if (!krk_parseArgs(".V",(const char*[]){"value"}, &value)) return NONE_VAL();
	struct PHeap * node = malloc(sizeof(struct PHeap));
	node->owner = self;
	node->value = value;
	self->heap = pheap_insert(self->heap, node, run_comparator);
	self->count += 1;
	return value;
}

KRK_Method(PHeap,peek) {
	if (self->heap) return self->heap->value;
	return NONE_VAL();
}

KRK_Method(PHeap,pop) {
	PHeap * old = self->heap;
	if (!old) return krk_runtimeError(vm.exceptions->valueError, "pop from empty heap");
	self->heap = pheap_delete_min(self->heap, run_comparator);
	self->count -= 1;
	return old->value;
}

KRK_Method(PHeap,__bool__) {
	return BOOLEAN_VAL(self->heap != NULL);
}

KRK_Method(PHeap,__len__) {
	return INTEGER_VAL(self->count);
}

static void run_visitor(PHeap * heap, void * visitor) {
	krk_push(*(KrkValue*)visitor);
	krk_push(heap->value);
	krk_callStack(1);
}

KRK_Method(PHeap,visit) {
	KrkValue func;
	int after = 0;
	if (!krk_parseArgs(".V|p",(const char*[]){"func","after"},
		&func, &after)) return NONE_VAL();

	(after ? pheap_visit_heap_after : pheap_visit_heap)(self->heap, run_visitor, &func);

	return NONE_VAL();
}

static void _scan_one(PHeap * heap, void * unused) {
	krk_markValue(heap->value);
}

static void _pheap_scan(KrkInstance * _self) {
	struct PHeap_Obj * self = (void*)_self;
	krk_markValue(self->comparator);
	pheap_visit_heap(self->heap, _scan_one, NULL);
}

static void _free_one(PHeap * heap, void * unused) {
	free(heap);
}

static void _pheap_sweep(KrkInstance * _self) {
	struct PHeap_Obj * self = (void*)_self;
	pheap_visit_heap_after(self->heap,_free_one, NULL);
}


KrkValue krk_module_onload__pheap(KrkString * runAs) {
	KrkInstance * module = krk_newInstance(vm.baseClasses->moduleClass);
	krk_push(OBJECT_VAL(module));

	KrkClass * PHeap = krk_makeClass(module, &PHeapClass, "PHeap", vm.baseClasses->objectClass);
	PHeap->allocSize = sizeof(struct PHeap_Obj);
	PHeap->_ongcscan = _pheap_scan;
	PHeap->_ongcsweep = _pheap_sweep;

	BIND_METHOD(PHeap,__init__);
	BIND_METHOD(PHeap,insert);
	BIND_METHOD(PHeap,peek);
	BIND_METHOD(PHeap,pop);
	BIND_METHOD(PHeap,__bool__);
	BIND_METHOD(PHeap,__len__);
	BIND_METHOD(PHeap,visit);
	krk_finalizeClass(PHeapClass);

	return krk_pop();
}
