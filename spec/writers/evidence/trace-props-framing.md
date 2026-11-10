# Props record framing (NEXT-ROUND B) and the per-file `114/Props` records (NEXT-ROUND C question 1): static answer complete, writer routine NOT yet traced

_Wed 11 Nov 2026, round 2 of the TRACE agent.  The static part uses only the public fixtures on the host (`propsdiff.py`, `propsdiff2.py`, `propsgrow.py`, `hdr.py` in this folder; no names printed: the title record is shown as a length and a short hash).  The dynamic part is from the VM's baseline traces.  Not scanned or committed._

## 1. Which `114/Props` records differ file to file (C question 1), static

161 records in every file (tail at 3328 to 3346 depending on the title length).  Across the five calculate controls' **null saves** (`wave30-calculate-control-null` and the four `wave35-calculate-<pair>-control-null`), **8 of the 161 records differ; the other 153 are byte-identical**:

| record id | kind | payload | across the five files | class |
|---|---|---|---|---|
| `0x02400003` | 4 | 4 | project **Finish** word (`d827f63b`, `d827fb3b`, `d827fc3b`, `d827093c`, `c012183d`): equals the project row's Finish at save time | state, **rewritten by Calculate** |
| `0x02400006` | 4 | 4 | project start as set (`c012183d` in wave 30, `c012263d` in all wave 35 files) | per-file |
| `0x02400007` and `0x024013b1` | 4 | 4 | equal to each other in every file; differ per file (`ba04263d`, `c404263d`, ...) and, in the wave 30 null save only, change between control and null (`282d183d -> 322d183d`) | per-file stamp (a time of day); per-save in wave 30 |
| `0x02400008` | 0 | 58 to 76 bytes | the project title (UTF-16; the length is the only thing that moves the tail offset 3328 to 3346) | per-file identity |
| `0x02400029` | 0 | 16 | differs in every file **and** changes in every null save (control to null) | per-save id/stamp |
| `0x024013c2` | 0 | 16 | same: per file and per save | per-save id/stamp |
| `0x02401394` | 0 | 16 | differs per file, does **not** change between a control and its null save | per-file identity (creation stamp or id) |

Control to null save (per-save noise), per file: wave 30 `02400007 02400029 024013b1 024013c2`; every wave 35 file `02400029 024013c2` only (the two 16-byte records).

Control to edited (Calculate) save, every file: `0x02400003`, `0x02400024`, `0x02400029`, `0x024013c2`, except `snet` which has no `0x02400003` change (the project Finish does not move).  `0x02400003` takes the new project Finish word exactly (unlinked `d827fd3b` = 3bfd27d8 = the project row's Finish in the setter table of `trace-calculate-unlinked.md`; linked `d827023c`; lag `d827033c`).  **`0x02400024` is a 2-byte flag `0100 -> 0000`: the "calculation pending" flag, cleared by Calculate**; the baseline pair clears it too (`01` to `00` at stream offset 838).

Consequence for the port's Props pin (finding R35cG0-3): a Calculate writer needs, per file, the 16-byte `0x02400029`/`0x024013c2` regenerated per save (any value; they are per-save noise), `0x02400003` from the new project Finish, and `0x02400024` cleared; the other 153 records are identical across five different files, so the pin need not be the measured control file's bytes for those, only for the 8 above.  (Not done: whether the 153 are also identical for a file that has resources or assignments; the five controls have none or placeholders only.)

## 2. The framing, from the static data

Every record is `length(4) id(4) kind(4) payload(length)` (`hdr.py`: `0x02400024` is `02 00 00 00 24 00 40 02 02 00 00 00 | 01 00`, `0x02400003` is `04 00 00 00 03 00 40 02 04 00 00 00 | 20 1c f6 3b`), and the stream header's first two words are `stream_size - 4`.  In the baseline pair the header size word moves `0x15a9a -> 0x15b08` (+110) and the declared-entries word `0x106 -> 0x107`.  The 110 new bytes are not a clean insertion: aligning the control and the edit gives 28 differing bytes before offset 3210 (the size words, the declared-entries word, the pending flag at 838, a date word at 2320, a 3-byte stamp and a 16-byte stamp near 2849 to 2889) and a poor alignment after it, so the growth is spread through the typed-property section and is not decoded here.

## 3. Dynamic: the writer of the framing

Baseline trace `C:\trace\runs\baseline` (round 1, recording started after the file was opened): the record `0x02400024` (`02 00 00 00 24 00 40 02 02 00 00 00 01 00`) is present at four heap addresses at the end of the trace (the loaded blob and its copies), and `TTD.Memory` write queries on its length, id, kind and payload bytes return **no write at all inside the recording** (`trace4`, "NOWRITE").  So the framing of an unchanged record is not produced by any instruction in a recording that starts after open: it is loaded from the file at open and written back by a copy at save.  A recording that starts before `FileOpenEx` is needed to see who builds the loaded form, and the grown part of the baseline stream is the place to look for a framing write at save time.

A new capture was taken for this: `trace-save5.ps1` records from before `FileOpenEx` (`baseline-open`: 1172 MB, saved 242176 bytes, gate 96 streams, 93 checked, **18 problems**, the same set as round 1's baseline: `114/Props 41`, `Task/FixedData 27`, `Task/Var2Data 28`, `Assn/Var2Data 30`, `Rsc/FixedMeta 2`, the Fixed2Data stamps; block not transcribed).  The memory search of that trace found no hit in the heap range tried (0x40000000 bytes around the first setter call's table object); the next attempt should locate the stream's in-memory copy through the docfile cache as in `trace-calculate.md` ("view 2"), not by a heap scan.

## 4. What the next agent should do

1. In `C:\trace\runs\baseline-open`, find the Props blob's first write (the reader), then the save-side copy (`FUN_140377770` family, `+0x1a6`/`+0x1e7`), and run `trace4` on the 110 new bytes of the saved stream for the first non-copy store: that is the routine that writes a framed record with a length, id, kind and payload.
2. Confirm `0x02400003` and `0x02400024` as store sites: the 4-byte and 2-byte payload writes in the Calculate trace (`trace-calculate.md`); `trace4` from their positions in the saved stream.
3. Decode the baseline's 110 grown bytes (properly aligned) before writing a framing rule.
