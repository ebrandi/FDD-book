# Driver Handoff Checklist

A driver is rarely owned by the same person forever. Whether
you are handing a driver off to a new maintainer, leaving a
company, or simply transferring stewardship to a colleague,
the items below help ensure the transition is clean.

Use the checklist both as a handoff document and as a
self-review. Running through it regularly will surface the
gaps in your own maintenance practice long before you actually
need to hand the driver off.

**Driver:** _________

**Current maintainer:** _________

**Proposed new maintainer:** _________

**Expected handoff date:** _________

---

## Source and Build

- [ ] The driver builds from a clean tree with no local
  modifications required.
- [ ] The build succeeds on every architecture the driver is
  expected to support.
- [ ] The build produces no new warnings at any warning level
  that the project uses by default.
- [ ] The driver integrates cleanly with `make buildkernel`
  and `make buildworld`.

## Documentation

- [ ] The manual page is current and reflects real behaviour.
- [ ] The manual page passes `mandoc -Tlint`.
- [ ] A short design document explains the non-obvious
  decisions and the hardware-specific quirks.
- [ ] The `sysctl` variables and `dev.` names are documented
  with their meaning and their expected range of values.
- [ ] The module's `MODULE_DEPEND` relationships are
  explained or obviously correct.

## Testing

- [ ] The driver has a regression test suite that the new
  maintainer can run on their own hardware.
- [ ] The regression tests have been run successfully within
  the last month.
- [ ] The driver has been tested against at least two FreeBSD
  releases.
- [ ] The compatibility range is documented.
- [ ] Known failure modes are documented, including any
  hardware configurations that are known not to work.

## Hardware

- [ ] The hardware required to test the driver is described
  in enough detail that the new maintainer can acquire or
  borrow it.
- [ ] Any vendor-specific documentation, datasheets, or NDAs
  that affect the driver are recorded, either with the
  driver or in a known location.
- [ ] Any hardware in the maintainer's possession that should
  travel with the driver is packaged and ready for
  shipment.

## Open Work

- [ ] All TODOs, known bugs, and planned work are recorded in
  an issue tracker or a text file in the repository.
- [ ] Any outstanding patches or reviews have been surfaced
  to the new maintainer.
- [ ] Any pending conversations with the community, vendors,
  or users have been summarised.
- [ ] Any scheduled deprecations or upstream changes that
  will affect the driver soon are flagged.

## Community Context

- [ ] The new maintainer has been introduced to the relevant
  mailing-list discussions.
- [ ] The new maintainer has been introduced to any
  community members who have contributed to the driver.
- [ ] The new maintainer has been added to any access
  controls (Phabricator, Bugzilla, GitHub) the role
  requires.
- [ ] The change in maintainership has been announced on the
  appropriate mailing list.

## Privacy

- [ ] The commit history contains no proprietary information
  that would prevent further public development.
- [ ] Any internal tooling, internal URLs, or internal ticket
  references have been scrubbed from the public repository.
- [ ] Credentials, API keys, and secrets are not present in
  the repository history.

---

## Free-Form Handoff Notes

Use the space below to capture anything that does not fit in
the boxes above: personal context the new maintainer might
want, the history of major decisions, expected future direction,
or any final words.

_________

_________

_________
