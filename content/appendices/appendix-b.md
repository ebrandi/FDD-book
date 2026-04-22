---
title: "Algorithms and Logic for Systems Programming"
description: "A pattern guide to the data structures, control flow, and reasoning idioms that recur throughout FreeBSD kernel and driver code."
appendix: "B"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 30
---

# Appendix B: Algorithms and Logic for Systems Programming

## How to Use This Appendix

The main chapters teach the primitives a driver author uses day to day. Alongside those primitives there is a second layer of knowledge that the book assumes without always naming it: the small catalogue of data structures, control-flow shapes, and reasoning patterns that show up in nearly every driver you will read. A red-black tree in a virtual-memory subsystem, a `STAILQ` of pending requests on the side of a softc, a `goto out` ladder that unwinds an `attach` routine, a switch on a state enum that drives a protocol handshake. None of these ideas is hard in isolation. What makes them feel hard is meeting them for the first time embedded in real kernel code, where the pattern is implicit and the name is nowhere on the page.

This appendix is that missing name. It is a short, practical field guide to the algorithmic patterns that recur across FreeBSD kernel and driver code, organised so that you can recognise a pattern when you see it and decide which pattern fits when you have to write one. It is not a computer-science textbook, and it is not a replacement for the chapters that already teach the specific pieces you will use most. What it provides is a middle layer: enough pattern recognition to let you read driver code confidently, enough mental modelling to pick the right structure when the blank page stares back at you, and enough caveat awareness to avoid the mistakes that every driver author eventually makes once.

### What You Will Find Here

The appendix is organised by problem family, not by abstract taxonomy. Each family collects a few related patterns with a consistent short structure:

- **What it is.** One or two sentences.
- **Why drivers use it.** The concrete role in kernel or driver code.
- **Use this when.** A compact decision hint.
- **Avoid this when.** The other side of the same decision.
- **Core operations.** The handful of names you need in muscle memory.
- **Common traps.** The mistakes that actually cost people time.
- **Where the book teaches it.** A pointer back to the main chapters.
- **What to read next.** A manual page or a realistic source-tree example.

When a pattern has a real FreeBSD implementation with a stable interface, the entry uses those names directly. When the pattern is generic, the entry still grounds itself in how FreeBSD drivers actually apply it.

### What This Appendix Is Not

It is not a first course in algorithms. If you have never met a linked list or a state machine before, this appendix will feel too compressed; read Chapters 4 and 5 first, then come back. It is not a deep theoretical reference either. You will find almost no asymptotic analysis here, because driver authors rarely choose between O(log n) and O(n) on paper. They choose between patterns whose trade-offs are mostly about locking, cache behaviour, and invariants. Finally, it is not a replacement for Appendix A (which covers the APIs), Appendix C (which covers hardware concepts), or Appendix E (which covers kernel subsystems). If the pattern you want points you there, go there and come back.

## Reader Guidance

Three ways to read this appendix, each calling for a different strategy.

If you are **writing new code**, skim the problem family that matches your situation, read the one or two entries that apply, glance at the **Common traps** line, and close the appendix. Thirty seconds is enough. Start from the pattern, not from the code.

If you are **reading someone else's driver**, treat the appendix as a translator. When you see an unfamiliar idiom, find the family that names it, read **What it is** and **Core operations**, and keep moving. Full understanding can come later; right now the goal is to form a mental model of what the driver is doing.

If you are **debugging**, read the **Common traps** lines in the pattern family around the bug. Most driver bugs that look mysterious are ordinary caveats the author did not honour. A ring buffer with a stale `full` check, an unwind ladder that frees in the wrong order, a state machine that forgot a transient state. The traps in this appendix are not exhaustive, but they cover the ones that recur.

A few conventions apply throughout:

- Source paths are shown in the book-facing form, `/usr/src/sys/...`, matching a standard FreeBSD system.
- Manual pages are cited in the usual FreeBSD style. For the data-structure macros, the authoritative pages live in section 3: `queue(3)`, `tree(3)`, `bitstring(3)`. For kernel services such as `hashinit(9)`, `buf_ring(9)`, `refcount(9)`, the pages live in section 9. The distinction matters only when you type `man` at the prompt.
- When an entry points to real source as a reading example, it points to a file that a beginner can open and navigate. Larger subsystems exist that also use the pattern; those are mentioned only when they are the canonical place to see a pattern in practice.

With that in mind, we start with the structures a driver uses to hold state.

## Data Structures in the Kernel

A driver without state is rare. Almost every non-trivial driver keeps a collection of something: pending requests, open handles, per-CPU statistics, per-channel configuration, per-client contexts. The question is which collection shape matches the access pattern. The kernel ships a small toolkit of header-file data structures that you should reach for by default instead of rolling your own. Each of them solves a different problem; confusing them is one of the most common category errors in driver design.

### The `<sys/queue.h>` Family

**What it is.** A family of intrusive linked-list macros. You define a head and an entry, embed the entry inside your element structure, and the macros give you insertion, removal, and traversal without heap allocation for list nodes.

The header defines four flavours:

- `SLIST` is a singly linked list. Cheapest, forward only, O(n) removal of arbitrary elements.
- `LIST` is a doubly linked list. Forward and backward traversal, O(1) removal of an arbitrary element.
- `STAILQ` is a singly linked tail queue. Forward only, but with fast insertion at the tail. Classic FIFO.
- `TAILQ` is a doubly linked tail queue. Every operation you can reasonably want, at the cost of two pointers per element.

**Why drivers use them.** Almost every per-driver collection in the tree is one of these. Pending commands, open file handles, registered callbacks, child devices. They are predictable, allocation-free at the list level, and the macros make the code read almost like pseudocode.

**Use this when** you have a collection of objects you already allocate yourself, and you want standard list operations without inventing them. The intrusive design means an object can live in several lists at once; that is a feature, not a bug.

**Avoid this when** you need ordered lookup by key. A list is O(n) to search. Reach for a tree or a hash table instead.

**Choosing among the four.**

- Start with `TAILQ` unless you have a reason not to. It is the most common and the most flexible.
- Drop to `STAILQ` when you know you only ever push and pop like a queue. You save one pointer per element.
- Use `LIST` when you want doubly linked behaviour but do not need a tail pointer. Unordered sets of independent elements, for example.
- Use `SLIST` when every byte counts and the list is short or traversal is linear anyway. Fast paths in hot structures sometimes justify it.

**Core operations.** The macros are uniform across flavours. Insertion: `TAILQ_INSERT_HEAD`, `TAILQ_INSERT_TAIL`, `TAILQ_INSERT_BEFORE`, `TAILQ_INSERT_AFTER`. Removal: `TAILQ_REMOVE`. Traversal: `TAILQ_FOREACH`, plus the `_SAFE` variant when you may remove the current element. The same operations exist with `LIST_`, `STAILQ_`, and `SLIST_` prefixes. Field storage is an embedded `TAILQ_ENTRY(type)` in your struct.

**Common traps.**

- Using the non-`_SAFE` foreach while removing elements. Plain `TAILQ_FOREACH(var, head, field)` expands to a `for` loop that reads `TAILQ_NEXT(var, field)` at the end of each iteration. If the body freed `var`, the next step dereferences freed memory. `TAILQ_FOREACH_SAFE(var, head, field, tmp)` caches the next pointer into `tmp` before the body runs, and is always the right answer when the loop body may remove `var`.
- Forgetting that the list is intrusive. The same element cannot be in two `TAILQ`s through the same entry field at once. If you need that, define two `TAILQ_ENTRY` fields with different names.
- Mixing flavours on the same head. `SLIST_INSERT_HEAD` on a `TAILQ_HEAD` compiles but produces silent corruption. Keep the macros consistent with the head type.

**Where the book teaches this.** Introduced briefly in Chapter 5 as part of the kernel dialect of C and revisited whenever a driver in later parts of the book needs an internal collection.

**What to read next.** `queue(3)` for the full macro catalogue, and any real driver that keeps pending work. `/usr/src/sys/net/if_tuntap.c` uses these idioms extensively.

### Red-Black Trees via `<sys/tree.h>`

**What it is.** A set of macros that generate a self-balancing binary search tree inlined into your types. FreeBSD's implementation is rank-balanced, but it exposes the historical `RB_` prefix and behaves like a red-black tree for all practical purposes.

**Why drivers use them.** When you need O(log n) ordered lookup by a key and the key is not a small integer, a red-black tree is the default. Virtual-memory maps, scheduler run-queues in some subsystems, and per-subsystem registries of named objects all use this header.

**Use this when** you have a collection of objects indexed by a key, the set grows to hundreds or thousands of entries, and you care about ordered traversal as well as lookup.

**Avoid this when** the set is small (a dozen elements) or the key space is small and dense. A linear scan on a cache-hot array usually beats a tree in both cases, because the constant factor of a pointer chase is much higher than a sequential read.

**Core operations.** `RB_HEAD(name, type)` defines the head type, `RB_ENTRY(type)` defines the embedded node field, `RB_PROTOTYPE` and `RB_GENERATE` instantiate the function family (insert, remove, find, min, max, next, prev, foreach). You also write a comparison function that returns negative, zero, or positive the way `strcmp(3)` does. A variant `SPLAY_` family in the same header gives splay trees when locality of reference is important.

**Common traps.**

- Calling `RB_GENERATE` in a header. That produces multiple definitions at link time. Keep `RB_GENERATE` in exactly one `.c` file and `RB_PROTOTYPE` in the header that declares the type.
- Forgetting that the tree is intrusive. An element cannot live in two trees through the same entry, and removing from the wrong tree silently corrupts both.
- Holding a pointer across an insert. Balance rotations do not move nodes, but the comparator must be consistent; if your key changes while the node is in the tree, you have invalidated the search structure and every subsequent lookup is undefined.

**Where the book teaches this.** Not taught as a dedicated pattern in the book. The reader meets it through example when Appendix E walks the virtual-memory subsystem and when advanced drivers in Part 7 reach for ordered lookup.

**What to read next.** `tree(3)` for the macro catalogue, and any subsystem that keeps an ordered set of named things.

### Radix Trees via `<net/radix.h>`

**What it is.** A kernel-specific radix (or Patricia) tree, primarily used by the routing table to match addresses against prefixes. A radix tree differs from a red-black tree in that it keys on prefix length rather than total order, which is precisely what longest-prefix-match needs. The header lives at `/usr/src/sys/net/radix.h`, alongside the rest of the networking code, rather than under `/usr/src/sys/sys/`.

**Why drivers use them.** Rarely, directly. The radix tree exists because the networking stack needs it, and the machinery is specialised to that use. A driver almost never creates its own radix tree. You may meet one as a caller in network-adjacent code, or you may see the related LinuxKPI radix-tree shim in compatibility layers.

**Use this when** you genuinely need prefix matching over keys of varying length. Routing, ACL tables, prefix-based policy.

**Avoid this when** what you actually need is ordered lookup by exact key. Use `<sys/tree.h>` instead.

**Core operations.** `rn_inithead`, `rn_addroute`, `rn_delete`, `rn_lookup`, `rn_match`, `rn_walktree`. These are specialised enough that the routing subsystem is the canonical reference.

**Common traps.** The header and the routines are tightly coupled to the routing-table abstraction. Attempting to reuse the radix tree for non-routing data structures is almost always a mistake; the abstraction will leak in uncomfortable ways. The LinuxKPI radix tree in `/usr/src/sys/compat/linuxkpi/common/include/linux/radix-tree.h` is a different structure and should not be confused with the native one.

**Where the book teaches this.** Only mentioned in Appendix E alongside the routing table. This appendix names it so you can recognise it in passing.

**What to read next.** The routing-table code under `/usr/src/sys/net/`.

### Bitmaps and Bit-Strings via `<sys/bitstring.h>`

**What it is.** A compact set-of-bits abstraction backed by an array of machine words. You allocate a bit-string of N bits, then set, clear, test, and scan individual bits.

**Why drivers use them.** Any time you have a dense finite universe and want to track boolean state for each element. Free-slot bitmaps for a fixed pool of hardware descriptors, interrupt allocations, minor numbers, per-channel enable flags. A bitmap uses one bit per element and supports efficient first-free searches.

**Use this when** the universe is dense, known in advance, and you track boolean state per element. Bitmaps are particularly effective when you need "find the first free slot" or "how many are set".

**Avoid this when** the universe is sparse. A set of a few thousand used slot numbers in a 32-bit key space does not belong in a bitmap; use a hash table or a tree.

**Core operations.** `bit_alloc` (kernel allocator variant) and `bit_decl` (stack), `bit_set`, `bit_clear`, `bit_test`, `bit_nset`, `bit_nclear` for ranges, `bit_ffs` for first-set, `bit_ffc` for first-clear, `bit_foreach` for iteration. Sizes are computed with `bitstr_size(nbits)`.

**Common traps.**

- Forgetting that bit indices are zero-based and that the `bit_ffs` / `bit_ffc` macros do not return a value; they write the found index into `*_resultp` and set it to `-1` when nothing matched. Always check `if (result < 0)` before using the index.
- Race conditions under concurrent modification. `bitstring(3)` is not atomic; if two contexts can set or clear bits concurrently, they need a lock or a coordinated handoff.
- Allocating the wrong size. Use `bitstr_size(nbits)` rather than doing the division by hand.

**Where the book teaches this.** Not taught directly. A driver that needs a free-slot bitmap will reach for this header, and this entry is the pointer.

**What to read next.** `bitstring(3)`.

### Hash Tables with `hashinit(9)`

**What it is.** A small helper pattern for building hash tables out of `<sys/queue.h>` lists. `hashinit(9)` allocates a power-of-two array of `LIST_HEAD` buckets and gives you back the array and a mask. Your code hashes the key to a 32-bit value, ANDs it with the mask, and walks the resulting bucket.

**Why drivers use them.** When you need unordered lookup by key and the set is large enough that a linear scan is too slow. File-system name caches, open-file tables per-process, and any driver that maintains a registry keyed by hashable identifier all use this shape.

**Use this when** the set is large, lookups are frequent, and ordering does not matter. A hash table beats a tree on average-case lookup and is easier to lock coarsely (one lock per bucket, or one lock for the whole table if writes are rare).

**Avoid this when** the set is small (a tree or a list is simpler) or you need ordered traversal (`RB_FOREACH` is the answer, not hashing).

**Core operations.** `void *hashinit(int elements, struct malloc_type *type, u_long *hashmask);` returns the bucket array and writes the mask to `*hashmask`. Each bucket is a `LIST_HEAD`, so you insert and iterate with the usual `LIST_` macros. `hashdestroy` tears it down. `hashinit_flags` takes an extra flags word (`HASH_WAITOK` or `HASH_NOWAIT`) when you need the non-sleeping variant; `hashinit` itself is equivalent to `HASH_WAITOK`. `phashinit` and `phashinit_flags` give you a prime-sized table when you prefer modulo arithmetic to masking, and write the chosen size (not a mask) through the out-pointer. A small `<sys/hash.h>` header provides basic hash functions such as `hash32_buf` and `hash32_str`, along with `jenkins_hash` and `murmur3_32_hash`; the Fowler-Noll-Vo family lives in `<sys/fnv_hash.h>`.

**Common traps.**

- Hashing a pointer directly. Pointer low bits are usually aligned and produce poor bucket distribution. Use a proper hash function on the contents or on the shifted pointer.
- Assuming `hashinit` uses your requested size. It rounds down to the nearest power of two and returns the mask for that size. A request for 100 elements produces a 64-bucket table, not 100.
- Forgetting the lock story. A bare hash table is not thread-safe. Decide up front whether you want a single lock, per-bucket locks, or something fancier, and document it in the softc.

**Where the book teaches this.** Briefly referenced in Part 7 drivers that maintain a per-client registry. This entry gives the scaffolding for when you need to build one yourself.

**What to read next.** `hashinit(9)`, and the implementation in `/usr/src/sys/kern/subr_hash.c`.

## Circular and Ring Buffers

Rings appear everywhere in a driver that mediates between hardware and software. A NIC hands out a ring of descriptors for packet transmission. A UART buffers characters between the interrupt that receives them and the process that consumes them. A command driver batches requests into a ring for the hardware engine to pick up. The shape is always the same: a fixed-size array plus a producer index and a consumer index that wrap around. What varies is who is concurrent with whom, which primitives you use, and whether the ring needs to be lock-free.

### Single-Producer Single-Consumer Ring Buffers

**What it is.** A fixed-size circular buffer where exactly one context produces and exactly one context consumes. The producer advances a `head` (or `tail`) index after writing; the consumer advances a `tail` (or `head`) index after reading; neither ever writes the other's index.

**Why drivers use them.** The interrupt handler produces data (a received character, a completion event), and a thread consumes it. That is the prototypical SPSC shape. It is lock-free by construction, because no two contexts modify the same variable.

**Use this when** you have exactly one producer and exactly one consumer, and you want to avoid a lock. Character device ring buffers are the classic case.

**Avoid this when** more than one thread can produce, or more than one thread can consume. Adding a second producer breaks the invariant; you need a lock, or you need `buf_ring(9)`.

**Core operations and invariants.** The usual invariants are:

- `head` advances only by the producer; `tail` advances only by the consumer.
- `(head - tail) mod capacity` is the number of occupied slots.
- `capacity - (head - tail) mod capacity` is the number of free slots (minus one, depending on the encoding).
- The buffer is empty when `head == tail`.
- The buffer is full when `(head + 1) mod capacity == tail`. The one sacrificed slot is how you distinguish full from empty.

The alternative encoding uses a separate `count` variable or uses free-running indices modulo the capacity with a mask (`head & mask`, `tail & mask`). Free-running indices make full-versus-empty a signed comparison and avoid the sacrificed slot.

**Common traps.**

- Mixing up full and empty without the one-slot convention. Every ring design must have a clear answer; pick one and write it at the top of the file.
- Not ordering memory properly. On modern CPUs, writes can become visible out of order. The producer must make sure the buffer contents are visible to the consumer before the updated `head` is. In the kernel that means an explicit memory barrier (`atomic_thread_fence_rel` on the producer, `atomic_thread_fence_acq` on the consumer), or using the atomic store-release and load-acquire variants.
- Assuming `capacity` is a power of two when the code does not enforce it. Masking requires a power of two; modulo does not. The code and the invariant must agree.

**Where the book teaches this.** Chapter 10 builds a circular buffer for the `myfirst` driver's character device and teaches the head/tail/full/empty discipline from first principles. Revisited in Chapters 11 and 12 when the ring meets synchronisation.

**What to read next.** The circular-buffer example from Chapter 10 in the companion `examples/` tree, and any small character-device driver that uses a ring to bridge interrupt and process context.

### `buf_ring(9)` for MPMC Rings

**What it is.** FreeBSD's kernel ring-buffer library. `buf_ring` provides a multi-producer multi-consumer ring with lock-free enqueue and both single-consumer and multi-consumer dequeue paths. It is used heavily by iflib, networking drivers, and elsewhere in the tree.

<!-- remove after Chapter 11 revision is durable -->
See Chapter 11 for the introduction and the `buf_ring(9)` man page for the authoritative concurrency contract.

**Why drivers use them.** When more than one context can enqueue (several CPUs pushing packets onto the same transmit queue) or more than one context can dequeue (several worker threads draining the same work queue), you cannot get away with the SPSC invariants. `buf_ring` does the atomic compare-and-swap work for you and hides the memory-ordering details behind a stable interface.

**Use this when** you have multiple producers or multiple consumers and you want a lock-free path. Network-adjacent drivers are the canonical case.

**Avoid this when** you truly have one producer and one consumer. The SPSC ring is simpler, cheaper, and easier to reason about.

**Core operations.** `buf_ring_alloc(count, type, flags, lock)` and `buf_ring_free` at setup and teardown. `buf_ring_enqueue(br, buf)` enqueues, returning `0` on success or `ENOBUFS` when full. `buf_ring_dequeue_mc(br)` dequeues safely under multiple consumers; `buf_ring_dequeue_sc(br)` is faster when the caller guarantees single-consumer semantics. `buf_ring_peek`, `buf_ring_count`, `buf_ring_empty`, `buf_ring_full` give you inspection without removing.

**Common traps.**

- Calling `buf_ring_dequeue_sc` from multiple consumers at once. The SC path assumes only one consumer is running; violating that corrupts the tail index silently.
- Treating `ENOBUFS` as a hard error. `buf_ring_enqueue` returning `ENOBUFS` is an ordinary back-pressure signal; the caller must either retry, drop, or queue on a slower path.
- Forgetting that `buf_ring_count` and `buf_ring_empty` are advisory. Another CPU may enqueue or dequeue the moment after you check. Design the caller around eventual consistency, not instantaneous truth.

**Where the book teaches this.** Named in Chapter 10 as the production-grade counterpart to the pedagogical SPSC ring. Used in Part 5 networking chapters when driver examples need real ring semantics.

**What to read next.** `buf_ring(9)`, and the header itself at `/usr/src/sys/sys/buf_ring.h`. For a real user, browse iflib or any high-performance NIC driver under `/usr/src/sys/dev/`.

### Wrap-Around Arithmetic and the Full-vs-Empty Problem

Every ring has to distinguish full from empty. The two states satisfy `head == tail` or are one slot apart, and the encoding you pick decides which comparison to use. The two common conventions are worth naming so you can spot them in code.

- **Sacrificed slot.** The ring holds `capacity - 1` usable slots. Empty is `head == tail`. Full is `(head + 1) mod capacity == tail`. Simple, cheap, works with any capacity.
- **Free-running indices.** `head` and `tail` count every operation since the ring was created and never wrap. The modular index for array access is `head & mask` (which requires `capacity` to be a power of two). Occupied count is `head - tail` as unsigned arithmetic; full is `occupied == capacity`; empty is `occupied == 0`. The advantage is that you never sacrifice a slot; the cost is that you must write `(uint32_t)(head - tail)` carefully.

Both conventions are correct. Pick one and make it a one-sentence comment at the top of the file. Readers who know ring buffers will know what to look for; readers who do not will have the comment.

## Sorting and Searching

Drivers sort and search less often than they list, ring, or tree things. But when they do, a small toolkit covers almost every case, and knowing which tool is appropriate saves time.

### In-Kernel `qsort` and `bsearch`

**What it is.** The kernel provides the familiar `qsort` and `bsearch` from the standard library, declared in `/usr/src/sys/sys/libkern.h`. A thread-safe variant `qsort_r` passes a user context pointer through to the comparator.

**Use this when** you need to sort an array in place once or occasionally, or when you need binary search over a sorted array. Device ID tables, compatibility matching, and sorted snapshots all fit here.

**Avoid this when** the collection is long-lived and changes frequently. In that case you want a tree, not a sort-and-search pair. Also avoid `qsort` on hot paths: the function-call overhead of the comparator dominates for small arrays.

**Core operations.** `qsort(base, nmemb, size, cmp)` and `qsort_r(base, nmemb, size, cmp, thunk)` for sorting; `bsearch(key, base, nmemb, size, cmp)` for searching. The comparator returns negative, zero, or positive in the `strcmp(3)` style. All three are declared in `libkern.h` and have section-3 manual pages (`qsort(3)`, `bsearch(3)`).

**Common traps.**

- Non-stable comparisons. `qsort` is not guaranteed stable. If ordering among equal keys matters, add a tiebreaker to the comparator.
- Passing a comparator that tests with subtraction (`a->x - b->x`) when the fields are wide. Integer overflow can make a negative look positive. Use explicit comparisons (`a->x < b->x ? -1 : a->x > b->x ? 1 : 0`).

**Where the book teaches this.** Referenced in Chapter 4 (the C tour) and in Part 7 where specific drivers sort a table once at attach.

**What to read next.** `qsort(3)`, `bsearch(3)`.

### Binary Search Over Device ID Tables

**What it is.** A specialised application of `bsearch`. A driver maintains a sorted table of `(vendor, device)` or compatible-string keys, and the probe path does a binary search to decide whether the hardware is one it supports.

**Why drivers use it.** A PCI driver may match dozens of device IDs. A scan through the table is fine at attach time; doing it linearly works. Doing it with `bsearch` is a small, readable, fast improvement when the table grows.

**Use this when** the ID table is longer than a handful of entries and ordering by key is easy. You can sort at compile time by declaring the table statically sorted, or once at module load with `qsort`.

**Avoid this when** the match is not a simple key comparison. PCI matching sometimes involves subclass and programming interface fields; express the matching logic directly rather than contorting it into a comparator.

**Common traps.** Sorting at runtime and then relying on the sort for ABI compatibility. If another piece of code reads the table by index, changing the order breaks it. Declare the table `const` and sort at build time when you can.

**Where the book teaches this.** Referenced in Chapter 18 (PCI) and in the device-matching discussion in Chapter 6.

**What to read next.** Any PCI driver's ID table and probe path. `/usr/src/sys/dev/uart/uart_bus_pci.c` is a readable example.

### When Linear Search Wins

**What it is.** A reminder that for small collections, linear search on a cache-hot array is the fastest lookup you can write. CPU caches reward sequential reads and punish pointer chasing.

**Use this when** the collection has fewer than a few dozen entries. In practice, a linear scan on twenty items is faster than any tree lookup, because the tree walk takes several cache misses while the linear scan takes one.

**Avoid this when** the collection grows without bound, or when the hot-path latency requirement is tight and the worst case matters more than the average.

**Common traps.** Optimising the wrong side. Do not introduce a tree to cure "linear search is slow" before measuring. The real cost is usually elsewhere.

## State Machines and Protocol Handling

Many drivers are state machines in disguise. A USB device probes, attaches, goes idle, resumes, suspends, detaches. A network driver goes through link-down, link-up, negotiation, steady-state, error recovery. When the state transitions are complicated, making the state explicit pays off immediately. When they are simple, an enum and a switch often do the job.

### Explicit State Enum vs Implicit Flag Bits

**What it is.** Two ways to represent state. An enum says "this device is in exactly one of N states". A flag word says "this device has any combination of N independent booleans".

**Use the enum when** the conditions are mutually exclusive and naming the transitions matters. A link is one of `LINK_DOWN`, `LINK_NEGOTIATING`, or `LINK_UP`. A command is one of `CMD_IDLE`, `CMD_PENDING`, `CMD_RUNNING`, or `CMD_DONE`. An enum forces every transition to be explicit; you cannot accidentally be in two states at once.

**Use flag bits when** the conditions are independent and can be combined. "Interrupt enabled", "autonegotiation allowed", "in promiscuous mode". Mixing mutually exclusive and independent conditions in the same flag word is the usual mistake; separate them into an enum for the first and a flag word for the second.

**Common traps.**

- Encoding a state machine as bit flags and then having to decide what "both `CMD_PENDING` and `CMD_DONE` set" means. That never means anything useful. Switch to an enum.
- Encoding independent booleans as a single packed state. You will reinvent bitwise OR, badly.

### Switch-Based FSMs

**What it is.** The simplest explicit state machine: an enum for the state, a switch in a function that takes an event, and a body per case that updates the state and performs the action.

**Use this when** the state space is small (perhaps ten states) and the transitions are shallow. A switch is easy to read, easy to extend, and the compiler catches missing cases if you enable the right warning flag.

**Avoid this when** the switch grows to hundreds of lines or the same transition logic is duplicated across several states. That is the signal to move to a table-driven FSM.

**Common traps.**

- Forgetting a state. Compile with `-Wswitch-enum` so the compiler tells you.
- Doing non-trivial work inside the switch. Keep the switch as a dispatcher; delegate the real work to named helper functions.

**Where the book teaches this.** The pattern appears naturally in many driver examples. Chapter 5 introduces it in its simplest form.

### Table-Driven FSMs

**What it is.** The state machine expressed as a two-dimensional table indexed by `(state, event)`. Each cell holds the next state and, typically, a function pointer for the action to run on that transition.

**Use this when** the FSM has many states and many events and you want the transition logic to be data, not code. Visualising the matrix is often easier than chasing a giant switch. Protocol stacks, bus enumeration logic, and suspend-resume state machines are classic uses.

**Avoid this when** the FSM is small. A five-state switch is easier to read than a five-by-five matrix.

**Core operations.** Define the state enum, the event enum, and a transition struct containing `next_state` and `action_fn`. Declare the table as a static constant two-dimensional array. The driver steps the machine with `table[current_state][event]`.

**Common traps.**

- Null action pointers without a check. Either the table must be dense (every cell is a valid transition) or the dispatcher must treat a null action as "ignore", and the convention must be consistent.
- Embedding locks inside the action functions without a clear discipline. The state transition is a critical section; decide whether the caller holds the lock or the dispatcher takes it, and stick to that.

### Function-Pointer Dispatch

**What it is.** A lighter alternative to a full state table. The current "mode" of the driver is a pointer to a set of handler functions; switching state is as simple as reassigning the pointer.

**Use this when** the "state" is really a set of methods the driver performs differently depending on mode. A link that comes up has a different receive path than a link that is still negotiating. A device that is booting its firmware dispatches commands differently from one that is already running.

**Avoid this when** the state transitions are ad-hoc or numerous. A function-pointer swap is a coarse operation; reserving it for a few well-named modes keeps the code honest.

**Common traps.**

- Racing with the swap. If one CPU calls through the pointer while another is replacing it, the call may dispatch to stale code. The swap needs ordering. In the kernel, that usually means a lock or an RCU-style deferred free via `epoch(9)` for any state structure the old pointer referenced.
- Orphaned resources. When you swap the function-pointer table, make sure the resources the old mode owned are handed off or released cleanly. The switch itself does not free anything.

### Reentrancy and Partial Completion

**What it is.** A state-machine design concern rather than a data structure. Real drivers receive events in interleaved order: an interrupt fires while the driver is halfway through processing an ioctl; a completion arrives while the command that triggered it is still being posted; a detach races with an open. The state machine has to tolerate this.

**Design principles.**

- Make every transition a single, atomic update. Read current state, compute next state, commit with a lock or an atomic. Do not perform the action before committing the state; if you crash or return early, the state must match reality.
- Separate "what to do" from "who runs it". The state transition decides what needs to happen; a taskqueue or a worker thread actually does the work. That way the transition can finish before the expensive work starts, and a second caller sees consistent state.
- Have a "transient" state for every transition that takes time. A command that is in the act of being sent is neither `IDLE` nor `RUNNING`; it is `SENDING`. A link that is bringing itself down is neither `UP` nor `DOWN`; it is `GOING_DOWN`. Transient states give concurrent callers something to wait for or retry on.

**Common traps.** Assuming events are serialised. They are not. An interrupt can arrive at any time, a detach can race with an open, a userspace close can race with a device's internal teardown. Build the state machine so that every event is handled from every state, even if the handling is just "reject" or "defer".

**Where the book teaches this.** Revisited in Chapter 11 (synchronisation), Chapter 14 (interrupts in depth), and Part 5 when real networking drivers hit these cases hard.

## Error-Handling Patterns

A driver that gets the happy path right but gets the error path wrong will corrupt memory, leak resources, or panic the kernel. Good error handling in driver code is almost always about structure, not cleverness. The kernel has a strong, uniform idiom for it, and this section names that idiom.

### The `goto out` Idiom

**What it is.** The canonical kernel cleanup pattern. At the top of the function, declare every resource to `NULL`. At each acquisition, check the result; on failure, `goto fail_N` where `fail_N` is a label that releases exactly the resources acquired so far. At the end of the function, the success path returns; the cleanup ladder runs in reverse order of acquisition and falls through every label down to the final return.

**Why drivers use it.** Because it works. The ladder makes it impossible to forget a release, and the order-of-release is visually identical to the reverse of the order of acquisition. The pattern is pervasive in the FreeBSD tree.

**Use this when** a function acquires more than one resource. Three is usually the threshold where a ladder is cleaner than nested `if`.

**Avoid this when** the function is simple. A single acquisition, a single release, and a single error path do not need a ladder; a straight-line function with two returns is clearer.

**The shape.**

```c
int
my_attach(device_t dev)
{
    struct my_softc *sc;
    struct resource *mem = NULL;
    void *ih = NULL;
    int error, rid;

    sc = device_get_softc(dev);
    sc->dev = dev;

    rid = 0;
    mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
    if (mem == NULL) {
        device_printf(dev, "cannot allocate memory window\n");
        error = ENXIO;
        goto fail_mem;
    }

    error = bus_setup_intr(dev, sc->irq, INTR_TYPE_MISC | INTR_MPSAFE,
        NULL, my_intr, sc, &ih);
    if (error != 0) {
        device_printf(dev, "cannot setup interrupt: %d\n", error);
        goto fail_intr;
    }

    sc->mem = mem;
    sc->ih  = ih;
    return (0);

fail_intr:
    bus_release_resource(dev, SYS_RES_MEMORY, rid, mem);
fail_mem:
    return (error);
}
```

The shape is what matters. Each label releases exactly the resources acquired between it and the start of the function. Falling through is intentional; the labels are stacked in the order a human would read them from the top of the function.

**Variants.**

- A single `out:` label with conditional checks (`if (mem != NULL) bus_release_resource(...)`) works for short functions. For long ones, the numbered-label variant is easier to audit because it encodes the order directly.
- Some code names the labels `err_mem`, `err_intr` instead of `fail_mem`, `fail_intr`. The prefix does not matter; consistency within a file does.

**Common traps.**

- Freeing in the wrong order. Always free in reverse order of allocation. The ladder's visual order is a reminder, not a proof; review the labels when you add a new resource.
- Initialising pointers to something other than `NULL`. If a ladder conditionally frees, the condition has to be reliable. Uninitialised pointers are undefined; free of uninitialised memory is a panic.
- Returning on the success path with a partial state. The success path at the end should either hand full ownership to the caller or return an error and unwind. There is no third option.

**Where the book teaches this.** Chapter 5 introduces the pattern explicitly and the book uses it consistently from Chapter 7 onward.

**What to read next.** Real attach functions under `/usr/src/sys/dev/`. Almost any driver shows the pattern in action.

### Return-Code Conventions

**What it is.** The kernel convention: integer return codes where `0` means success and a positive errno value means failure. `1` is not an error; it is a truthy return that would be confusing next to `EPERM` (which is also `1`). Always return `0` on success.

**Core conventions.**

- `0` on success, a positive errno (`EINVAL`, `ENOMEM`, `ENXIO`, `EIO`, `EBUSY`, `EAGAIN`) on failure.
- Never return a negative error code. That is a Linux convention and does not match the rest of FreeBSD.
- Propagate error codes upwards unchanged unless you can make them more specific.
- When a function returns a pointer, convention is to return `NULL` on failure and set a separate `int` if the caller needs to distinguish why.
- Probe routines follow a different rule: they return `BUS_PROBE_DEFAULT` and friends on success and `ENXIO` on no-match. See Appendix A for the full list.

**Common traps.**

- Returning `-1` from a kernel function. That is a userspace idiom; do not do it in kernel code.
- Overloading a single return value to mean both "an error" and "a resource count". Use a separate output pointer parameter if you need both.
- Swallowing errors. An error code from a lower-level call is a contract; either handle it or propagate it. Returning `0` after a failed `copyin` is how kernel panics are born.

**Where the book teaches this.** Chapter 5 again, alongside the cleanup ladder.

### Resource Acquisition and Release Ordering

**What it is.** The discipline that says: every resource you acquire in attach must be released in detach, in reverse order. Every lock you take must be released along every exit path. Every reference you add must be released before you drop the last one. The cleanup ladder above is a local expression of this principle; the whole driver is a global one.

**Core principles.**

- Attach acquires in order A, B, C. Detach releases in order C, B, A. The two are mirror images.
- Every error path inside attach unwinds what has been acquired so far, in reverse. That is the ladder.
- Locks are held for as short a time as possible and released along every exit, including the error paths. A `goto` ladder that unwinds memory but forgets the mutex is still wrong.
- Ownership should be local. One function acquires; the same function releases, or it hands ownership explicitly to the caller. A function that "leaks" ownership to a parameter side-effect is almost always a bug waiting to happen.

**Where the book teaches this.** Chapter 7 introduces the mirror-image discipline for a first driver, and every driver chapter afterwards repeats it.

## Concurrency Patterns

Two threads touching the same state is where kernel bugs are born. The synchronisation primitives in Appendix A are the tools; this section collects the patterns that use those tools well. The goal here is pattern recognition, not a full concurrency course.

### Producer-Consumer with Condition Variables

**What it is.** One context produces data (fills a buffer, posts a command, receives a character), and another consumes it. A shared state protected by a mutex, and a condition variable that the consumer waits on when the state is empty.

**Mental model.**

- The mutex protects the shared state. That is non-negotiable.
- The consumer checks the state under the mutex. If there is nothing to do, it sleeps on the condition variable via `cv_wait`. `cv_wait` atomically drops the mutex while sleeping and reacquires it on return.
- The producer also holds the mutex while it modifies the state, then signals the condition variable.
- The consumer re-checks the state after waking, because spurious wakeups and shared signals are possible. The check is always a loop, not an `if`.

**The shape.**

```c
/* Producer. */
mtx_lock(&sc->lock);
put_into_buffer(sc, item);
cv_signal(&sc->cv);
mtx_unlock(&sc->lock);

/* Consumer. */
mtx_lock(&sc->lock);
while (buffer_is_empty(sc))
    cv_wait(&sc->cv, &sc->lock);
item = take_from_buffer(sc);
mtx_unlock(&sc->lock);
```

**Common traps.**

- Using `if` instead of `while` around `cv_wait`. Spurious wakeups are real; always re-check.
- Signalling without holding the lock. FreeBSD permits it, but it is almost always easier to reason about if you signal while holding the lock.
- `cv_signal` when you meant `cv_broadcast`. `cv_signal` wakes exactly one waiter; if several waiters are waiting on different preconditions, only one of them will be unblocked, possibly the wrong one.
- Sleeping under a spinlock. `cv_wait` is sleepable; you need a normal mutex (`MTX_DEF`), not a spin mutex (`MTX_SPIN`).

**Where the book teaches this.** Chapter 10 builds the pattern implicitly around the circular buffer; Chapters 11 and 12 formalise it.

### Reader-Writer: `rmlock(9)` vs `sx(9)`

**What it is.** Two different read-write locks, with different cost profiles.

- `sx(9)` is a sleepable shared-exclusive lock. Readers block writers; writers block readers. Both sides may sleep inside the critical section.
- `rmlock(9)` is a read-mostly lock with an extremely cheap reader path (essentially a per-CPU indicator) and a much more expensive writer path (the writer must wait for every reader to drain).

**Choosing between them.**

- If reads are frequent, writes are rare, and the reader path must not sleep, choose `rmlock`.
- If reads are frequent, writes are occasional, and the reader path needs to sleep (for example, it calls `copyout`), choose `sx`.
- If reads and writes are roughly balanced or if the critical section is short, neither is right. Use a plain mutex.

**Core operations.**

- `sx`: `sx_slock`, `sx_sunlock` for shared; `sx_xlock`, `sx_xunlock` for exclusive; `sx_try_slock`, `sx_try_xlock`.
- `rmlock`: `rm_rlock(rm, tracker)`, `rm_runlock(rm, tracker)` for readers (each reader needs its own `struct rm_priotracker`, typically on the stack); `rm_wlock(rm)`, `rm_wunlock(rm)` for writers.

**Common traps.**

- Using `rmlock` for a balanced workload. The writer path is genuinely slow; use it only when reads dwarf writes.
- Letting an `rmlock` reader sleep without initialising with `RM_SLEEPABLE`. The default `rmlock` forbids sleeping in the read path.
- Upgrading a shared lock to an exclusive one. Neither `sx` nor `rmlock` supports a lock-free upgrade. Drop the shared lock, acquire the exclusive one, and re-check state.

**Where the book teaches this.** Chapter 12.

### Reference Counting with `refcount(9)`

**What it is.** A kernel-blessed pattern for counting how many contexts still use an object. An atomic counter, `refcount_acquire` to bump it, `refcount_release` to drop it and return `true` when the count reaches zero (so the caller can free).

**Why drivers use it.** Whenever an object can outlive the operation that produced it. A `cdev` that is being closed while a read is in flight; a softc whose detach races an ioctl; a buffer passed to hardware that must not be freed until the hardware is done.

**Use this when** you have shared ownership and no single place that can reliably free the object. If one piece of code unambiguously owns the object, skip the reference count and free it there.

**Core operations.** `refcount_init(count, initial)`, `refcount_acquire(count)` returning the previous value, `refcount_release(count)` returning `true` if the count reached zero (caller must free). There are `_if_gt` and `_if_not_last` variants for conditional acquisition, and `refcount_load` for read-only inspection.

**Common traps.**

- Using `refcount` for resources with a real ownership hierarchy. Refcounts are for genuinely shared ownership, not for laziness about who frees what.
- Freeing before the release. `refcount_release` returning `true` is your permission to free; do it inside that branch, not before.
- Mixing refcounts with locks. Refcount operations are lock-free, but "increment the refcount then dereference" is a race if another thread may drop the last reference between the two steps. The usual fix is to hold a lock while you decide to acquire.

**Where the book teaches this.** Mentioned in Appendix A, with the concurrency context built up through Chapters 11 and 12.

**What to read next.** `refcount(9)`, and any subsystem that manages shared lifetime.

## Decision Aids

The compact tables below are meant to be glanceable. They are not decision oracles; they are reminders of which family to look inside. The real decision is always local to the problem.

### Collection Shape

| You have... | Reach for |
| :-- | :-- |
| A small collection, no ordered lookup | `TAILQ_` or `LIST_` from `<sys/queue.h>` |
| A FIFO queue with no arbitrary removal | `STAILQ_` |
| A collection ordered by a key, grows large | `RB_` from `<sys/tree.h>` |
| A dense universe of boolean flags | `bit_*` from `<sys/bitstring.h>` |
| Unordered lookup on a large set | `hashinit(9)` plus `LIST_` |
| Prefix-matching networking keys | `radix.h` (you usually do not write this yourself) |

### Ring Buffer Shape

| You have... | Reach for |
| :-- | :-- |
| One producer, one consumer, lock-free | SPSC ring with head/tail indices |
| Many producers, many consumers | `buf_ring(9)` |
| Producer is an interrupt, consumer is a thread | SPSC ring plus condition variable or `selrecord` |
| Soft queue with back-pressure to userspace | SPSC ring plus `poll(2)` or `kqueue(2)` integration |

### State Representation

| Your state is... | Reach for |
| :-- | :-- |
| A handful of mutually exclusive modes | `enum` plus `switch` |
| Many states, many events, complex transitions | Table-driven FSM |
| Coarse modes that change the driver's methods | Function-pointer dispatch |
| Independent boolean facts | Flag bits in an integer |

### Cleanup Strategy

| Your function... | Reach for |
| :-- | :-- |
| Acquires one resource | Single early return, single release |
| Acquires two or three | `goto out:` with conditional `if (ptr != NULL)` |
| Acquires many, order-sensitive | Numbered `goto fail_N:` ladder |
| Has to roll back a mid-way success | Same ladder with explicit "rollback" label |

### Concurrency Pattern

| You have... | Reach for |
| :-- | :-- |
| Short critical section, no sleeping | `mtx(9)` with `MTX_DEF` |
| Short critical section in an interrupt filter | `mtx(9)` with `MTX_SPIN` |
| Many readers, occasional writer, may sleep | `sx(9)` |
| Many readers, rare writer, no sleep in readers | `rmlock(9)` |
| One producer, one consumer, waiting on an event | mutex plus `cv(9)` |
| Shared ownership of an object with no single freer | `refcount(9)` |
| A counted resource (pool of slots) | `sema(9)` |
| One-word counter or flag | `atomic(9)` |

## Wrapping Up: How to Recognise These Patterns in Real Code

Recognising a pattern in someone else's driver is a different skill from writing the pattern yourself. The easy part is spotting the API: `TAILQ_FOREACH`, `buf_ring_enqueue`, `goto fail_mem`. The harder part is spotting the pattern when the author has not used an API at all, because the pattern is pure control flow.

Three habits help.

The first is asking what invariant the author is trying to preserve. A ring buffer preserves "the interrupt and the thread never write the same index". A state machine preserves "we are always in exactly one state". A cleanup ladder preserves "every resource is released exactly once, in reverse order". When you read new code, naming the invariant first gives the rest of the code somewhere to attach to.

The second is reading the comment at the top of the file. Most FreeBSD drivers include a short block comment explaining the locking discipline, the state machine, or the ring conventions they use. That comment exists precisely because a reader cannot recover it from the code alone. Read it before you read the code, and read it again when the code surprises you.

The third is letting the patterns in this appendix colonise your vocabulary. When you find yourself writing "the ring buffer", be able to say whether it is SPSC or MPMC, whether the indices are sacrificed-slot or free-running, where the barrier belongs. When you find yourself writing "the state machine", be able to name the states, the events, and the transient states. When you find yourself writing "the cleanup path", be able to list the resources in reverse order of acquisition. The appendix becomes useful once these words come to mind automatically.

From here you can move in several directions. Appendix A has the detailed API entries for every primitive named above. Appendix C grounds the hardware-side patterns (DMA rings, descriptor arrays, and the discipline of coherency) that sit just below these software ones. Appendix E covers the kernel subsystems that many of these patterns were born inside. And every chapter of the book has labs where these patterns appear in a working driver, so that the next time you meet one in the wild you recognise it without slowing down. That is all pattern recognition really is: the confidence to keep reading.
