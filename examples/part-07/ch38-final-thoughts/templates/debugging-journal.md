# Debugging Journal Entry

Use one entry of this template for each bug that takes more
than a single short session to diagnose. Keep entries together
in a single file or folder, so that over time they become a
personal reference you can search.

The most valuable fields are the last two (what you learned and
what you would look at sooner next time). Fill them in even
when you feel the bug is self-explanatory.

---

**Date:** _________

**Module or driver:** _________

**Kernel version:** _________

**Hardware involved:** _________

---

## One-Line Description of the Symptom

_________

## What I Observed First

Describe the exact behaviour that made you realise something
was wrong. Include dmesg fragments, console output, or stack
traces when available.

_________

## First Hypothesis

What did you think the bug was, before investigating?

_________

## How I Tested the First Hypothesis

Describe the specific actions you took to test the hypothesis.

_________

## What Happened Instead

What did the test reveal? If the first hypothesis held, say
so. If it did not, describe the surprising observation.

_________

## Subsequent Hypotheses

If the first hypothesis was wrong, list each subsequent one,
with a short note on how you tested it and what you found.

- Hypothesis 2: _________

  Test: _________

  Result: _________

- Hypothesis 3: _________

  Test: _________

  Result: _________

## Root Cause

When you finally understood the real cause, what was it?

_________

## Fix

What change, exactly, made the bug go away? Include the commit
hash or the diff if you have it.

_________

## What I Learned

What do I know now that I did not know before this bug? This
field is the single most valuable one in the journal.

_________

## What I Would Look At Sooner Next Time

If I saw a similar symptom again, what is the first thing I
would check, knowing what I know now?

_________

---

## References

- **Relevant source paths and line numbers:**

  _________

- **Relevant mailing-list threads or commits:**

  _________

- **Relevant bug IDs:**

  _________

- **Documentation I wish had existed:**

  _________

---

## Follow-Ups

- [ ] Write a regression test that would have caught this.
- [ ] Update any manual page affected by the fix.
- [ ] Share the finding with anyone who might hit the same
  bug.
- [ ] Close the bug report if one was filed.
