# Project's RichEdit note serialisation

_W34-0.2 decode note, 30 October 2026; rule completed by the six W34-0.3
note pairs. Companion to the pair reports under [`evidence/`](evidence/).
Static evidence is from the git-ignored Ghidra model. No dynamic trace this
wave (W34-0.4 decision)._

## Rule

A task or resource note is a Var2Data block whose payload is one complete RTF
byte string — not plain text. The two measured fields are:

| field | key | container |
|---|---|---|
| Task/Notes | `0x0b40000f` | `114/TBkndTask` |
| Resource/Notes | `0x0c400014` | `114/TBkndRsc` |

Both payloads use the same envelope:

```
prefix, 159 bytes:
  {\rtf1\ansi\ansicpg1252\deff0\nouicompat\deflang1033
  {\fonttbl{\f0\fnil\fcharset0 Segoe UI;}}\r\n
  {\*\generator Riched20 <version>}\viewkind4\uc1 \r\n
  \pard\f0\fs20<space>
escaped text
suffix, 10 bytes:
  \par\r\n}\r\n\0
```

So `payload_length = 159 + len(escaped text) + 10`. The block is appended at
the current end of Var2Data and costs `4 + payload_length`; a 12-byte VarMeta
entry is added at its sorted `(uid, key)` position (see
[`var-meta.md`](var-meta.md)). The Var2Data block's length word is the
payload length; the final NUL is inside the payload.

The escaped text follows RichEdit's RTF stream-out, measured on five new
pairs:

- `\` → `\\`, `{` → `\{`, `}` → `\}`. The wave-34 escape note
  (`Back\slash and {braces} end}`, 28 characters) becomes the 32-byte region
  `Back\\slash and \{braces\} end\}` in a 201-byte payload.
- A non-ASCII character becomes `\u<decimal>?` (six bytes per character):
  `Café naïve résumé.` (18 characters) becomes
  `Caf\u233? na\u239?ve r\u233?sum\u233?.`, 38 bytes, in a 207-byte payload.
  `\ansicpg1252` stays.
- A line break becomes an inline `\par\r\n` paragraph separator. The
  two-line note `First line of the note.\r\nSecond line.` (37 characters
  including the CRLF) becomes 23 + 6 + 12 = 41 bytes, in a 210-byte payload.
  Project's `Task.Notes` getter returns the CRLF as a single `\r`.
- The suffix's leading `\par\r\n` closes the last paragraph; a multi-line
  note therefore has the same suffix as a single-line note.

The envelope is the Windows RichEdit control's own RTF stream-out:
`\ansicpg1252`, a one-entry font table for the default UI font (`Segoe UI`),
`\deff0\nouicompat`, `\viewkind4\uc1`, `\pard\f0\fs20`, and a generator
string. Project does not compose it (see the static section). **The generator
version is environment state**: the wave-30 fixtures carry
`Riched20 16.0.20430` and every wave-34 fixture carries
`Riched20 16.0.20527`, so a Windows update between the waves changed it. A
note writer may only enable the measured shape and must reproduce or refuse
the generator string of the saving machine (the W34-4.1 condition).

Writing over an existing note is the same append rule, not an in-place
rewrite (measured by `wave34-note-replace`):

- The old block stays in Var2Data, orphaned and byte-identical at its old
  offset.
- The new block is appended at the old stream end; Var2Data grows by
  `4 + new payload`.
- The note's VarMeta entry is re-pointed to the new offset; the entry count
  and the VarMeta stream length are unchanged, and `Task FixedMeta` does not
  change at all (the field was already present).

Clearing a note removes its VarMeta entry and does not shrink anything
(measured by `wave34-note-clear`):

- VarMeta's logical count falls by one and every later entry shifts up by 12
  bytes; the stream keeps its length and its last slot becomes a stale
  duplicate of the new last entry (the same non-truncating removal rule as
  [`var-meta.md`](var-meta.md)).
- The block stays in Var2Data and Var2Data does not change at all.
- `Task FixedMeta` entry 5 (the task row with the note) changes three bytes:
  `+2` field count `0x0c → 0x0b`, `+14` `0x77 → 0x67`, `+44` `0x10 → 0x00`.
  Setting a note from nothing changes the same three bytes in the opposite
  direction; replacing a note leaves them alone.
- Two changes in one save (`wave34-note-two`: clear task 2's note and replace
  task 3's) compose: count 44 → 43, task 2's entry deleted, task 3's entry
  re-pointed from 715 to the appended 906 block (195 bytes), Var2Data
  906 → 1105 (+199), VarMeta length unchanged at 552 B with the stale
  duplicate tail slot, and the same three FixedMeta bytes as the clear pair.

## W35-3.3: the generator string comes from the saving machine

Wave 35 measured where the generator version comes from
(`scripts/oracle_w35_g3.py` pair `notes-generator`, log
`build/w35/g3-run4.log`; anatomy
[`evidence/wave35-g3-notes-generator.md`](evidence/wave35-g3-notes-generator.md)).
The committed `wave30-notes.mpp` (task 2's note, `Riched20 16.0.20430`) was
opened on a copy in Project 2021, saved, and reopened as the control, then task 3's
Notes was set to a synthetic line and the edit saved.

* The control and its null save keep the old note byte for byte
  (`16.0.20430`, payload at 520, 198 bytes).
* The edit appends a new 190-byte block at payload offset 722 whose generator
  is `Riched20 16.0.20527`, the current build of the machine that saved it.  The
  file's own note was not consulted.
* Comparing the two 159-byte prefixes, the only bytes that differ are payload
  offsets 124, 125 and 126, the last three digits of the build number; the font
  table, `\deff0`, `\viewkind4\uc1`, `\pard\f0\fs20` and the 10-byte suffix are
  identical.

So the four things the brief names are settled as follows.  The RTF header, the
font table (`Segoe UI`, one entry) and the escaping are closed by the eight
measured pairs, and they are byte-identical across two RichEdit builds.  The
generator string is not a property of the file: a port that copied the file's
generator would have written `16.0.20430` on this pair and Project wrote
`16.0.20527`.

The bytes that depend on the saving machine are the 10-character version in the
generator (`16.0.20527`, payload offsets 117 to 126; offsets 124 to 126 are the
ones measured to change).  If a machine's version string has a different length,
the payload length, the block's length word, `Var2Data`'s length, every later
VarMeta offset and the `114/Props` size word move with it.  The font name is
the same on both measured builds; whether it follows the machine's UI font is not
measured.

Under the W34-4.1 condition (reproduce or refuse the saving machine's
generator) the port can do neither on every machine it runs on: it cannot read
the version Project's RichEdit would write, and the file does not hold it.  Notes
stays disabled.

## Evidence

### Measured (pairs)

- `wave30-notes` — `114/TBkndTask/Var2Data` 516 → 718 B, one block appended
  at 516 with a 198-byte payload (`114/TBkndTask/VarMeta` 528 → 540 B, entry
  `[21] uid 2 key 0x0b40000f off 516`). The probe prints the payload as:
  the 159-byte prefix, then `A wave 30 notes fixture line.` (29 bytes), then
  `\par\r\n}\r\n\0` (10 bytes) = 198 B.
- `wave30-resource-notes` — `114/TBkndRsc/Var2Data` 140 → 332 B, one block
  appended at 140 with a 188-byte payload; `114/TBkndRsc/VarMeta` 12 → 13
  entries, new entry `[9] uid 2 key 0x0c400014 off 140`. The payload is the
  same 159-byte prefix, the 19-byte text `Resource note line.`, and the same
  10-byte suffix = 188 B.
- `wave34-note-escape` — the escaped-backslash/braces rule: payload 201 B at
  offset 524, Var2Data 516 → 729, VarMeta 528 → 540 B (entry [21] uid 2 key
  `0x0b40000f` off 524), FixedMeta entry 5 `+2 0x0b → 0x0c`, `+14 0x67 →
  0x77`, `+44 0x00 → 0x10`.
- `wave34-note-nonascii` — `\uN?` with decimal codepoints, payload 207 B,
  Var2Data 516 → 735; same three FixedMeta bytes.
- `wave34-note-multiline` — inline `\par\r\n`, payload 210 B, Var2Data
  516 → 738; same three FixedMeta bytes.
- `wave34-note-replace` — control note 28 characters (197-byte payload at
  524); edit appends a 40-character note (209-byte payload) at 725,
  Var2Data 725 → 938 (+213), VarMeta 540 B with entry [21] re-pointed
  524 → 725, FixedMeta unchanged, old block at 524 orphaned.
- `wave34-note-clear` — control note 19 characters (188-byte payload at
  524); edit: VarMeta 43 → 42 entries in an unchanged 540-byte stream with a
  stale duplicate tail slot, Var2Data unchanged at 716 B, block orphaned,
  FixedMeta entry 5 `+2 0x0c → 0x0b`, `+14 0x77 → 0x67`, `+44 0x10 → 0x00`.
- `wave34-note-two` — control notes on tasks 2 and 3 (187-byte payloads at
  524 and 715); edit clears task 2 and replaces task 3: VarMeta 44 → 43
  entries in an unchanged 552-byte stream, task 3's entry re-pointed
  715 → 906, new 195-byte block at 906, Var2Data 906 → 1105 (+199), the same
  three FixedMeta bytes as the clear pair.
- The `wave34-*` payloads carry the generator `Riched20 16.0.20527`; the
  wave-30 payloads carry `16.0.20430`. The rest of the prefix and the suffix
  are byte-identical across all eight notes (task and resource containers
  alike).

### Static (Ghidra model)

- The note envelope is not assembled by any Project routine in the model.
  `RichEdit20W` (`0x140fcece0`) is referenced by the window-creation
  functions `FUN_140267448`, `FUN_140326640`, `FUN_14036ccb0`,
  `FUN_140370740` and `FUN_140923500`; `Rich Text Format` (`0x140f6fdb8`) is
  only the clipboard format name in `FUN_1401cc64c`.
- The two functions that reference the string `generator`
  (`FUN_1404c9ffc`, `FUN_1405e3520`, address `0x1410ce798`) are HTML/OA table
  exporters — their other strings are `CreateTableE2oFromHtmlTable: …`,
  `CreateOATable: …`, `htmlTableCols`, `keepSourceFormat`. They are not note
  writers.
- `RES_RTF_NOTES` (`0x1411083b0`, `FUN_140ec5800`) is a Project Server
  ResourceDataSet field (`RES_AVAILABILITY`, `RES_RATES_TABLE`, the
  `…/ResourceDataSet/` namespace); it is an export, not the `.mpp` writer.
  The class `.?AVCRtfConv@@` (`0x141392c20`) is a converter, not the note
  serialiser.
- The read side has an error code `PROJBKND_E_BADRTF` (`0x1410df7b0`, no
  referencing function), confirming the backend parses the RTF payload when
  it loads a note.
- Conclusion: the serialiser is the OS RichEdit control's stream-out, which
  is why the generator version and default font are machine facts. There is
  no Project function to point at, and none is needed: the measured envelope
  and escaping above cover the measured shapes.

### COM facts

The wave-30 and wave-34 oracles set the Notes field (`Task.Notes` and
`Resource.Notes`) to synthetic text through `scripts/oracle_lock.py`, then
Project saved. No COM call wrote an RTF payload. The replace and clear pairs
seed the note in one session, close the file, reopen it, and save the control
from the reopened file, so the edit operates on a note loaded from disk.

## Limits

- Tab characters, other control characters, multiple paragraphs beyond two,
  field codes, hyperlinks, bold/italic and any other formatting are
  unmeasured. Only `\pard`, `\f0`, `\fs20`, `\par` and the escaping above
  are observed.
- Non-ASCII is measured for Latin-1 characters that map to `\uN?`; a
  character outside the ANSI page, surrogate pairs and the `?` fallback
  character itself are unmeasured.
- The generator string (`Riched20 16.0.20527`) and the font (`Segoe UI`) are
  facts of the saving RichEdit build and the machine's UI font; the two-wave
  difference proves they change. The port must reproduce the measured
  envelope and refuse a file whose note envelope differs.
- The replace/clear measurements use notes created in a seed session, then
  reloaded; a note edited in the same session that created it is unmeasured
  (the pair design cannot distinguish that case).
- A note attached to an assignment or calendar is unmeasured; only the task
  and resource containers are known.
- The two-change pair changes two tasks; more than two changes in one save
  and changes that also add a note are unmeasured.
- The measured text lengths are 18, 19, 28, 29, 37, 40 and 40-plus; the
  length and escaping rules are verified on those points, not proven.

## Pairs still needed

1. **A note with a tab** and a note with three or more paragraphs — measures
   the remaining control words.
2. **A note with a character outside cp1252** (e.g. a CJK glyph) — measures
   the `?` fallback and whether the font table changes.
3. **A note edited in the same session that created it** — confirms the
   append/orphan rule does not depend on the note having been loaded.
4. **An existing note kept unchanged while another field changes** — confirms
   the note block is not re-serialised on an unrelated save (the
   `wave34-calculate-second` pair suggests no: zero note-stream effects).
5. **A note on an assignment or calendar**, if Project exposes one, to
   confirm the envelope is container-independent.

No dynamic trace this wave (W34-0.4 decision). A trace of the RichEdit
`EM_STREAMOUT` call would confirm the envelope source but cannot add a shape
the pairs do not show.
