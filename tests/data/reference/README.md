# Committed, client-free parity references

A reference here is derived from **MS Project itself** running a committed
synthetic fixture — never from a render of this port (the audit's A-1 verifier:
our own earlier render is not a reference).

**Operator ruling (Tim, 2026-10-23): Project screenshots stay local and never
reach the repository.**  Only the NUMBERS measured from them are committed.
The coordinator's local pre-push guard (not tracked in the repo) enforces this,
and the ruling stands for any clone regardless of the hook.

| file | what |
|---|---|
| `wave23-outline-project.measures.json` | `scripts/parity_report.py`'s measurements of a 1920x1040 Project Professional 2021 (16.0.20430.20182) capture of `tests/data/wave23-outline.mpp`, taken 2026-10-23 through `scripts/oracle_lock.py` from a COPY of the fixture, window maximised.  The PNG lives under `build/w23/` (git-ignored); this JSON is the structural reference the Windows CI gate compares the 1920x1032 render against.  SHA256 of this file: `121A594A256078F4AF16669E3390822A8899E8B44526A2E7C26C1FD44404C290`. |

The fixture is synthetic (`Summary A`, `Sub A1`, `Leaf A1a` ... `Resource A`);
no client data appears.  The PNG's own SHA-256 is recorded inside the JSON
(`image.sha256`) so a local capture can be identified without committing it.

Regenerate: capture the same fixture locally, run
`python scripts/parity_report.py --json <this file> <capture.png>`, sanitize the
`image.path` field, and update the hash in `scripts/check_fixtures.py` and here
in the same commit.
