# Project's RichEdit note serialisation

_W34-0.2 decode note, 30 October 2026. Companion to the pair reports under
[`evidence/`](evidence/). Static evidence is from the git-ignored Ghidra
model. No dynamic trace this wave (W34-0.4 decision)._

## Rule

A task or resource note is a Var2Data block whose payload is one complete RTF
byte string — not plain text. The two measured fields are:

| field | key | container |
|---|---|---|
| Task/Notes | `0x0b40000f` | `114/TBkndTask` |
| Resource/Notes | `0x0c400014` | `114/TBkndRsc` |

Both payloads use the same envelope, byte for byte except the text:

```
prefix, 159 bytes:
  {\rtf1\ansi\ansicpg1252\deff0\nouicompat\deflang1033
  {\fonttbl{\f0\fnil\fcharset0 Segoe UI;}}\r\n
  {\*\generator Riched20 16.0.20430}\viewkind4\uc1 \r\n
  \pard\f0\fs20<space>
text
suffix, 10 bytes:
  \par\r\n}\r\n\0
```

So `payload_length = 159 + len(text) + 10`, and the text is inserted verbatim
(1 byte per character in the measured samples). The block is appended at the
current end of Var2Data and costs `4 + payload_length`; a 12-byte VarMeta
entry is added at its sorted `(uid, key)` position (see
[`var-meta.md`](var-meta.md)). The Var2Data block's length word is the
payload length; the final NUL is inside the payload.

The envelope is the Windows RichEdit 4.1 control's own RTF stream-out:
`\ansicpg1252`, a one-entry font table for the default UI font (`Segoe UI`),
`\deff0\nouicompat`, `\viewkind4\uc1`, `\pard\f0\fs20`, and the generator
string `Riched20 16.0.20430`. Project does not compose it (see the static
section). The generator version and font name are therefore environment
facts of the saving machine, not project data.

Only plain single-line ASCII text is measured. Escaping of `\`, `{` and `}`,
non-ASCII characters, tabs, line breaks, multiple paragraphs, and any
formatting are unmeasured. A note writer may only enable the measured shape;
anything else is refused up front (the W34-4.1 condition).

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
- The two pair reports both describe the payload as "text-like payload, not
  shown"; the probe `build/w34/note_probe.py` prints the bytes. A
  byte-for-byte diff of the two payloads after removing the text is empty:
  the envelope is identical for the task and resource containers.

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
  above is complete for the plain-ASCII shape.

### COM facts

The wave-30 oracle set the Notes field (`Task.Notes` and `Resource.Notes`) to
one plain line through `scripts/oracle_lock.py`, then Project saved. No COM
call wrote an RTF payload.

## Limits

- Escaping is unmeasured: a text containing `\`, `{` or `}` will not be
  inserted verbatim by RichEdit.
- Non-ASCII text is unmeasured. With `\ansicpg1252` RichEdit may emit `\'xx`
  or `\uN` sequences; neither is measured.
- A multi-line note, a tab, a paragraph break, a list, bold/italic or any
  other formatting is unmeasured; `\pard` and `\par` are the only control
  words observed beyond the envelope.
- The generator string (`Riched20 16.0.20430`) and the font (`Segoe UI`) are
  facts of the saving RichEdit build and the machine's UI font. A different
  build or theme produces different bytes. The port must reproduce the
  measured envelope and refuse a file whose note envelope differs.
- Editing an existing note (the RTF already exists) is unmeasured: both
  pairs add a note where none existed. Does Project replace the block text
  and append the new block, or rewrite in place?
- The measured text lengths are 29 and 19 characters; the 1:1 length rule is
  inferred from two points.

## Pairs still needed

1. **Note text containing `\`, `{` and `}`** — measures RichEdit's escaping
   (`\\`, `\{`, `\}`).
2. **Note text with a non-ASCII character** (e.g. `é`) — measures `\'e9`
   versus `\u233?` and whether `\ansicpg1252` changes.
3. **A two-line note** (Enter between lines) — measures `\par` repetition
   and whether `\pard\f0\fs20` is re-emitted.
4. **Edit an existing note** (replace its text) — measures the replace path
   and whether the old block is orphaned (the table var-store append rule) or
   rewritten in place.
5. **A note on a third container** if Project exposes one (assignment or
   calendar), to confirm the envelope is container-independent.

No dynamic trace this wave (W34-0.4 decision). A trace of the RichEdit
`EM_STREAMOUT` call would confirm the envelope source but cannot add a shape
the pairs do not show.
