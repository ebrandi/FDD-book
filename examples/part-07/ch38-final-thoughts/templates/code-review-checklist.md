# Code Review Checklist

Use this checklist whenever you sit down to review a change,
whether the change is your own before submitting or someone
else's before approving. A consistent pass over the same set of
concerns catches more problems than a free-form read.

**Change under review:** _________

**Reviewer:** _________

**Date:** _________

---

## Understanding

- [ ] I understand what the change is trying to accomplish
  and can restate its purpose in one sentence.
- [ ] The commit message explains the "why" clearly.
- [ ] The commit message will still make sense to someone
  reading it five years from now.

## Scope

- [ ] The change is scoped appropriately: not so large that
  it cannot be reviewed carefully, not so small that the
  context is missing.
- [ ] The change does not mix unrelated concerns (cleanup
  plus feature, refactor plus bug fix).
- [ ] Any incidental cleanup that did sneak in is small
  enough to be worth keeping in the same commit.

## Correctness

- [ ] Every function added or modified has a clear
  responsibility and a clear contract.
- [ ] Error paths are handled rather than silently
  swallowed.
- [ ] Resources allocated in attach() are released in
  detach() in reverse order.
- [ ] Any new locks are named clearly and their scope is
  explicit.
- [ ] The lock order is consistent with existing code.
- [ ] There is no potential for use-after-free, leaked
  references, or double-free.
- [ ] Integer arithmetic is checked for overflow where
  appropriate.
- [ ] String operations use safe variants.

## Style

- [ ] The change passes `checkstyle9.pl`.
- [ ] Identifier names follow the project conventions.
- [ ] Comments explain only what is not obvious from the
  code itself.
- [ ] No commented-out code is present.

## Build and Test

- [ ] The change compiles on at least two architectures with
  no new warnings.
- [ ] The change does not break any in-tree test.
- [ ] The change has a test case or a clear reason why no
  test is possible.
- [ ] The change passes the author's regression test suite.

## Documentation

- [ ] Any manual pages affected by the change have been
  updated.
- [ ] Any `sysctl` or `dev.` names introduced are named
  consistently with existing conventions.
- [ ] Any user-visible behaviour change has a
  release-notes-worthy summary.

## Operational

- [ ] The change behaves correctly under load and unload
  cycles.
- [ ] The change does not introduce a regression in boot
  time, attach time, or power consumption.
- [ ] The change does not log excessively at default
  verbosity.

---

## Tone (When Reviewing Others)

- [ ] My comments are respectful.
- [ ] My comments are specific (pointing to a line, an
  assumption, or a source) rather than abrupt dismissals.
- [ ] When I propose a different approach, I explain why.
- [ ] I have separated "must change" from "nice to have"
  in my feedback.
- [ ] I have acknowledged at least one thing the author
  did well.

---

## Notes

Use the space below for specific comments, open questions, or
things you want to come back to after the first pass.

_________

_________

_________
