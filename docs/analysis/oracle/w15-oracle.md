# Wave-15 oracle — the gate for written `214/` and formula bytes

**Question.** Waves 14-15 proved the written bytes were *self-consistent* (our
decoder reads them back, no-op encodes are byte-identical). The open question was
whether **MS Project accepts them**. Project's automation cannot append a table
column (`TableEdit` replaces at `ColumnPosition` 0, rejects 8/9;
`Application.ColumnInsert` hangs; `Application.Tables()` wedges), so the only way
to test acceptance is to write the bytes ourselves and have Project re-save the
file.

**Tool.** `tools/oracle_gate` (new, opt-in: `-DBUILD_ORACLE_GATE=ON`):

    oracle_gate tests/data/synthetic-base.mpp corpus/probe/w15-gate.mpp

It parses `214/CTable`, appends a 115-byte column record for Number1
(`0x0b400057`, title "Number1") after the last column, re-encodes all four parts,
re-parses its own output, writes a `[Name]` formula into `114/Props`
(Text2 `0x0b400036`), and saves. Our readers confirm the fixture: 10 columns,
`col 9 field=0x0b400057 title="Number1"`, `formulas: 1`.

## Result — PASS (structural)

`w15-gate.mpp` opened in Project, which read 3 tasks and the name "Task A" with no
error, then re-saved it via `FileSaveAs` to `w15-gate-project.mpp`. Decoding
Project's own re-save:

| what | before Project | after Project's re-save |
|---|---|---|
| `214/CTable` columns | 10 | **10** |
| column 9 | `0x0b400057` "Number1", header `0c1008000801000000` | **identical** |
| `114/Props` formula | Text2 `= [MSPJ188743694]` (= `[Name]`) | **preserved** |

So Project parses, accepts and round-trips our appended column and our formula
record. The append layout and the formula record are no longer "ours only".

## The caveat, stated plainly

Project re-saved the file with the formula record intact, but reading the field
back gives an **empty value** — and that is true for **Project's own**
`w14-formula.mpp` as well (`Text2` and `Text2.GetField` are empty after
`CalculateProject` in both files). So the empty readback is a property/readback
behaviour of formula fields in this Project build, **not** a difference caused by
our bytes. What is proved is acceptance and round-trip, not display of a computed
formula value; proving the latter needs a Project-native fixture whose formula is
known to display (a different oracle question, not a defect in the writer).

## Re-run

    cmake -S . -B build\vs-w18 -DBUILD_ORACLE_GATE=ON
    cmake --build build\vs-w18 --config Release --target oracle_gate
    build\vs-w18\tools\oracle_gate\Release\oracle_gate.exe \
        tests/data/synthetic-base.mpp corpus/probe/w15-gate.mpp
    python build/w15_gate.py            # open + re-save through Project
    python scripts/extract_viewstate.py corpus/probe/w15-gate-project.mpp \
        build/dump-w15proj --all
    build\vs-w18\core\viewstate\Release\viewstate_report.exe --dump \
        --class CTable build/dump-w15proj

Fixtures stay in git-ignored `corpus/probe/`; the JSON/decoded output here is
numbers only.
