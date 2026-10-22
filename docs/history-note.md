# History note (2026-10-23)

This repository's history was **rewritten once**, when it was made public:

* 139 incremental commits were condensed to one commit per wave.
* The two client corpus projects were renamed to `project-a` / `project-b`
  throughout (content and filenames) so the published tree carries no client
  project names.
* The full private history is preserved locally in
  `build/pre-public-history.bundle` (git-ignored, never pushed).

**Consequence:** commit hashes quoted in `docs/handoff/*`, `docs/cost.md` and the
wave markers from before this note refer to the private history and no longer
resolve here. The claims they support are unchanged — each wave's handoff states
its evidence — but a reader cannot `git show` them. Where a hash is essential,
the handoff's prose and the ring of fixtures/tests are the substitute.

The bundle can be verified without restoring it:

    git bundle verify build/pre-public-history.bundle
