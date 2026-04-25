# Data-Structure Comparison Cheatsheet

Quick reference for choosing a collection in kernel or driver code.
Full teaching lives in Appendix B. This sheet only helps you pick.

## The question to ask first

What is the *dominant access pattern* for this collection? Not the
average one, not the most common bug report, the one you will do in the
hot path. Most of the wrong choices below come from designing for the
easy access and forgetting the hot one.

- Will I iterate the whole thing, or look up one element?
- Do I need order (sorted by key), or only presence?
- Do I add and remove from the middle, or only at the ends?
- Is the element set dense (all values from 0..N taken) or sparse?
- Do I need arbitrary removal from a cursor that is already in hand?

## Families at a glance

| Family | Header | Grows | Ordered? | Lookup | Arbitrary remove | Pointer overhead |
| :-- | :-- | :-- | :-- | :-- | :-- | :-- |
| `SLIST_` | `<sys/queue.h>` | Yes | No | O(n) | Only with prev ptr | 1 next |
| `LIST_` | `<sys/queue.h>` | Yes | No | O(n) | O(1) | 1 next, 1 prev ptr |
| `STAILQ_` | `<sys/queue.h>` | Yes | No (FIFO) | O(n) | Hard in middle | 1 next + tail |
| `TAILQ_` | `<sys/queue.h>` | Yes | No | O(n) | O(1) from node | 2 ptrs + tail |
| `RB_` tree | `<sys/tree.h>` | Yes | By key | O(log n) | O(log n) | Left, right, parent, colour |
| Bit string | `<sys/bitstring.h>` | Fixed | Dense indices | O(1) test | O(1) clear | 1 bit per slot |
| Hash table | `hashinit(9)` + `LIST_` | Yes | No | ~O(1) avg | O(1) from node | 1 bucket ptr per entry |
| Radix trie | `<net/radix.h>` | Yes | Prefix | O(key bits) | O(key bits) | Specialised; rarely driver code |

## Decision hints

Reach for **`TAILQ_`** by default. It covers most driver needs:
pending requests, per-channel contexts, a list of sub-devices. The
extra pointer compared with `LIST_` is almost never the problem that
sinks a driver.

Reach for **`STAILQ_`** when the discipline really is first-in,
first-out and you will almost never remove from the middle. The
single-linkage keeps each element one word lighter and encourages the
FIFO invariant.

Reach for **`LIST_`** when you want cheap O(1) removal from the middle
but do not need a tail pointer.

Reach for **`SLIST_`** only when you are certain removal from the
middle will not happen (or when memory pressure is extreme).

Reach for an **RB tree** when the collection will grow past a few
hundred entries and lookups happen on the hot path. If you have a
numeric key you plan to range-scan in order, the RB tree is the
straightforward answer.

Reach for a **bit string** when the universe of slots is dense and
bounded: tag numbers, interrupt indices, I/O vector positions.

Reach for a **hash table** when the identifier is large and opaque (a
name, a fd number, a pid), the set is large, and you do not need
ordered iteration.

Do not reach for **radix tries** unless you are writing networking
code. They exist for IP prefix matching; outside the network stack the
RB tree is almost always the right answer instead.

## Anti-patterns to avoid

Do not promote a `TAILQ_` to an RB tree just because the list got
longer than expected. Measure first. A list of 200 entries that is
walked rarely costs less than a tree.

Do not reinvent `<sys/queue.h>`. Rolling your own list macros makes
the driver harder to review and almost always misses a removal case.

Do not assume a hash table is faster than a tree. For a few thousand
entries, RB trees with no hashing overhead often win. Hashes shine
when the key domain is huge and lookups are the dominant cost.

Do not choose by theoretical complexity alone. Locking, cache
behaviour, and how the data flows through the driver matter more than
O-notation. The decision is usually local to the problem.

## Cross-references

- Appendix A: kernel API reference for the macro families.
- `queue(3)`, `tree(3)`, `bitstring(3)`, `hashinit(9)`.
- Appendix B, section **Data Structures in the Kernel**, for the full
  pattern discussion.
