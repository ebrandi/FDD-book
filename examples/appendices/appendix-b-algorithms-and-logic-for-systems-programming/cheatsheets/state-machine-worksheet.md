# State-Machine Design Worksheet

Fill this worksheet in **before** you write the state machine. If you
cannot complete it on paper, the code will not be simpler than the
paper.

## 0. One-line purpose

Write one sentence describing what this machine controls.

    Purpose: _______________________________________________________

A state machine that is hard to describe in one sentence is usually two
state machines wearing the same name.

## 1. State inventory

List every state. Each state should be a condition the driver can be
*in*, not an event it has just seen. Use noun-like names
(`CONNECTING`, `IDLE`, `ERROR`), not verb-like ones (`CONNECT`,
`RESET`).

    State 1: _______________________________________________________
    State 2: _______________________________________________________
    State 3: _______________________________________________________
    State 4: _______________________________________________________
    State 5: _______________________________________________________

If your list exceeds about eight, consider whether some of them are
actually orthogonal and belong in flag bits instead.

## 2. Event inventory

List every event that can drive a transition. Events come from three
common sources: the user (ioctl, open/close), the hardware (interrupt,
completion, timeout), and internal timers or callouts.

    Event 1: _______________________________________________________
    Event 2: _______________________________________________________
    Event 3: _______________________________________________________
    Event 4: _______________________________________________________
    Event 5: _______________________________________________________

## 3. Transition table

For each (state, event) pair, write the next state and the action to
perform. An empty cell means the event is either impossible in that
state or is to be ignored; mark which.

| State \\ Event |        |        |        |        |        |
| :------------- | :----: | :----: | :----: | :----: | :----: |
|                |        |        |        |        |        |
|                |        |        |        |        |        |
|                |        |        |        |        |        |
|                |        |        |        |        |        |
|                |        |        |        |        |        |

Conventions:

- A cell entry like `CONNECTED / send_ack()` means "transition to
  `CONNECTED` and call `send_ack()`".
- A cell entry like `-` means "no change, event ignored".
- A cell entry like `X` means "impossible; log and panic in DEBUG".

## 4. Initial state

What state does the machine start in after `attach`? What state does
it end in after successful `detach`?

    Initial state: ________________________________________________
    Terminal state (if any): ______________________________________

## 5. Transient states

A transient state is one the machine enters, performs an action in,
and leaves almost immediately. Transients are a common source of
surprise because the driver spends so little time in them that tests
miss them.

List your transients and the guarantees they provide.

    Transient 1: __________________________________________________
      Guarantees: _________________________________________________
    Transient 2: __________________________________________________
      Guarantees: _________________________________________________

## 6. Locking

What lock protects the state variable? What actions may be taken while
the lock is held? Which may not?

    State lock: ___________________________________________________
    Held during transition: _______________________________________
    Released before: ______________________________________________

Actions performed inside the transition should be fast. Long-running
work belongs in a taskqueue or callout, with the machine advancing
into a "waiting" state until the work completes and posts its event.

## 7. Error paths

For each error condition, which state do you enter? Is it a terminal
error (requires a reset) or a recoverable one (returns to `IDLE`)?

    Error 1: ______________________________________________________
      -> state: ___________________________________________________
      -> recoverable? Y / N
    Error 2: ______________________________________________________
      -> state: ___________________________________________________
      -> recoverable? Y / N

## 8. Observability

How does an operator inspect the current state? (A sysctl is the usual
answer.) How does an operator see recent transitions? (A small ring
buffer of recent (state, event, timestamp) tuples is a common pattern.)

    State visible via: ____________________________________________
    Transition log: _______________________________________________

## 9. Representation choice

Based on the filled-in worksheet, choose how to implement the machine.

- [ ] `enum` plus `switch`: up to ~6 states, ~6 events, transitions
      are obvious.
- [ ] Table-driven with a 2D array: more states, more events, or you
      want tests to operate on the table.
- [ ] Function-pointer per state: the action set changes radically
      between states, not just which action fires.
- [ ] Flag bits in an integer: what you actually have is not a state
      machine but a set of independent booleans. Go back to step 1.

## 10. Self-check

- [ ] Every (state, event) cell is filled or explicitly marked
      impossible.
- [ ] Every state has at least one incoming and one outgoing
      transition (terminal states excepted).
- [ ] Every transient state is reachable and exits.
- [ ] The machine reaches the terminal state from any initial state
      via some sequence of events.
- [ ] Locking is consistent across every transition.

## Cross-references

- Appendix B, section **State Machines**.
- Chapter 5, for `switch` idioms in kernel C.
- Chapter 10, for event-driven programming in drivers.
