# Contribution Checklist

Use this checklist the first time you send a patch to the
FreeBSD Project, and every time after that. It mirrors the
gates a reviewer will apply and helps you catch avoidable
issues before the reviewer has to point them out.

Treat every box as a blocker. Do not submit until each item is
ticked with confidence.

## Scope and Purpose

- [ ] My patch addresses a specific, well-defined issue or
  adds a specific, well-defined feature.
- [ ] The patch does not mix unrelated concerns.
- [ ] I can state the "why" of the change in one sentence.
- [ ] The change is the right size for reviewers to read
  carefully in a single sitting.

## Build Integrity

- [ ] The patch applies cleanly to the current tip of the
  relevant branch.
- [ ] The patch builds on amd64 without warnings.
- [ ] The patch builds on at least one other architecture
  (typically arm64) without warnings.
- [ ] The patch does not introduce new `make universe`
  failures.

## Style and Conventions

- [ ] `checkstyle9.pl` runs clean against every `.c` and `.h`
  file.
- [ ] Every sentence in the manual page starts on its own
  line.
- [ ] Identifier names follow the project's `snake_case`
  conventions.
- [ ] Return statements are parenthesised: `return (0);` not
  `return 0;`.
- [ ] No trailing whitespace is present.

## Testing

- [ ] I have run my pre-submission test pipeline end to end
  and every stage passes.
- [ ] I have performed at least 50 load/unload cycles with no
  panics, no leaks, and no unexpected messages in `dmesg`.
- [ ] I have run any relevant in-tree tests (for example
  Kyua suites) and they pass.
- [ ] If the change is subtle, I have written at least one
  test case that catches the specific behaviour.

## Documentation

- [ ] I have written or updated the relevant manual page.
- [ ] `mandoc -Tlint` is silent on the updated manual page.
- [ ] I have updated any README, UPDATING notice, or release
  note that the change implies.
- [ ] I have added or updated comments only where they
  genuinely clarify intent.

## Commit Message

- [ ] The subject line is in the form
  `<subsystem>: <brief description>`.
- [ ] The subject line is no longer than 72 characters.
- [ ] The body wraps at 72 columns.
- [ ] The body explains what the change does and why.
- [ ] The commit message ends with a `Signed-off-by:` line.
- [ ] The commit message does not reference internal company
  URLs, internal ticket IDs, or internal review tools.

## Privacy and Sensitivity

- [ ] The patch contains no proprietary information.
- [ ] The patch does not include private correspondence or
  mailing-list content.
- [ ] The patch does not embed credentials, API keys, or
  other secrets.
- [ ] The patch does not include debugging output or
  personal scratch files.

## Review Preparation

- [ ] I have identified the reviewer(s) I will request.
- [ ] I have chosen the appropriate submission channel:
  Phabricator for structured review, GitHub pull request or
  mailing list otherwise.
- [ ] I have written the review description as a
  one-paragraph summary, a design discussion, and a testing
  summary.
- [ ] I have named the hardware supported and the versions
  tested.
- [ ] I have invited feedback on the specific design
  questions that worry me.

## Long-Term Readiness

- [ ] I have a plan for responding to review feedback within
  a reasonable time.
- [ ] I have subscribed to notifications for the review.
- [ ] I understand that merging the patch is a beginning,
  not an end, and I am prepared to maintain the code.
- [ ] I have noted the UPDATING file so I can watch for
  tree-wide changes that will affect this code.
