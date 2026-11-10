# Props new-record framing (round 4): the writer is FUN_140253fe4 "set property by id", not-found branch

_Wed 11 Nov 2026, round 4 of the TRACE agent.  Public fixtures on the host plus the `baseline` trace (`C:\trace\runs\baseline`, recorded by trace-save3: edit and save inside the recording) in the VM.  Written to `build\trace-2021\` only; the coordinator merges into `spec/writers/props-records.md`._

**Hash-checked.**  Every VM output below came through the KVP channel with `sha OK` (first 12 hex digits per call): r4b4 `186BDC8590AE` (the 38b50c call list), r4b6 `589816CD8AA6` (disassembly of 0x253fe4), r4b11 `72C5AFA75E63` (positions of the branch addresses), r4b12 `502EDE943744` (registers and bytes at the store), r4b14 `F9BC4EEBEC49`, r4b15 `E724E7D37858` (string source).  The host-side static diff (`r4_p3.py`, `r4_p4.py`) reads the public fixtures `wave30-baseline-control.mpp` / `wave30-baseline-set.mpp` and is reproducible.  Interpretation is mine.

## 1. What the 110 new bytes are (static, public fixtures)

`114/Props` 88734 -> 88844 bytes.  A resync diff (`r4_p3.py`) of control against set shows the growth is **not** inside the 161 framed records or the typed-property section: both streams are identical in length up to the control's end, and the **110 bytes are appended at the very end of the stream** (earlier rounds' alignment searches failed because they looked for an insertion).  The other changed bytes are (control -> set): stream +0/+4 size words `0x15a9a -> 0x15b08` (+110), +12 the record count `0x106 -> 0x107`, +838 pending flag `01 -> 00`, +911 `a9 -> bd`, +2320 four bytes (`ffffffff -> 28 2d 18 3d`, a date word), +2849 and +2874 (a 1-byte and a 16-byte stamp), +3386 a word `0x020a -> 0x06ce`, +40290 `0x56 -> 0x196`, +62446 `07 -> 08`, +62494 `0x16 -> 0x7a`, +63502 `0x18c -> 0x474` (counts or sizes in the typed-property section, not decoded).

The appended 110 bytes are **one framed record**: `62 00 00 00 | 93 13 40 02 | 00 00 00 00 | <98 bytes>` = length 0x62, id **0x02401393**, kind 0, payload a UTF-16 string with a two-character NUL terminator (47 visible characters; it is Project's built-in baseline-calendar text, not user data).  12 header bytes + 98 payload = 110.

## 2. The writer (dynamic, `baseline` trace)

The record is created by the save-time Props routine already found in round 3, `FUN_140397c00` (call site 0x397cea), which walks the property ids and calls **`FUN_14038b50c` (RVA 0x38b50c, "set record by id")** once per id (658 calls in the trace; the call for 0x02401393 is at position **1feb4:220e**, r9 = 0x80, the value is a BSTR whose length is read with SysStringLen at 0x38b58a: size = length * 2 + 2).  0x38b50c switches on the VARIANT type and ends in:

**`FUN_140253fe4` (RVA 0x253fe4), the property container's "set value by id" (`this`=rcx, id=edx, type word=r8w, value pointer=r9, byte size=[rsp+0xb0])**:

1. Lookup: `call 0x2542b0` (RVA 0x254062) walks the records (loop 0x254350..0x2543f0, one iteration per record, 161 here).  Found -> overwrite in place (0x254097, `call 0x3735a0` memcpy of the payload to record+0xc; this is the round-3 path for 0x02400003 / 0x02400024).
2. **Not found (branch at 0x254075 -> 0x254114), traced for id 0x02401393 at positions 1feb5:1729 .. 1feb9:2258:**
   - `add word [rdi+8], 1` (**RVA 0x254122**): the blob header's record count word (blob +8; stream +12; `0x106 -> 0x107`).  rdi = blob base.
   - the blob is grown (calls 0x22ee34 / 0x22ede0 / 0x22eda0 compute the new size = old size + 12 + length, with a pad of 1 when the length is odd at 0x25418c; 0x211b30 reallocates) and the new total size is stored at **0x2541df** (`mov [rax],edi`, blob +0: `0x15a9a -> 0x15b08`; the stream's two leading size words are written from it when the blob is written out by `FUN_14038b6dc`).
   - the record is **appended at the old end of the blob** (offset = the old size word, here 0x15a9a; r15d at 0x25423a) with the header stores at **0x254240 `mov [rcx+4], r13d` (id), 0x254244 `mov [rcx+8], ax` (kind/type word from [rsp+0xa0]), 0x254248 `mov [rcx], ebp` (length), 0x25424a `mov [rcx+0xa], r14w` (flags word from [rsp+0xb8])**, then the payload `memcpy` to rcx+0xc (0x254097).  Hash-checked state at position 1feb9:2229: rcx = blob+0x15a9a, ebp = 0x62, r13 = 0x02401393, blob header dwords `0x15b08, 0x10000, 0x107, 4, 0x35400016 ...`; memory at rcx = `62 00 00 00 93 13 40 02 00 00 00 00` (then payload still to be copied).
   
   **Rule for the port:** a new Props record is `length(4) id(4) kind-word(2) flags-word(2) payload(length)` appended at the end of the blob; blob +0 (size) += 12 + length (+1 pad if odd), blob +8 (record count) += 1; the stream's two leading size words follow the blob size.  (Whether the record list is kept sorted: it is not; the new record goes to the end, after the typed-property section.)

## 3. The value

The BSTR is built by the same routine that produces the Calculate values (`FUN_1403694d0`, RVA 0x36957f; copy at 1feb4:2038 from a string at 1feb4:1d61 / 1e04, which was itself copied from a calendar-name string created at 3bf8:402 and 3b96:2d8): the text is the name of the project's baseline calendar.  Why the record exists only after Set Baseline is the routine's rule (a non-empty baseline-calendar name); the control has none.  Not traced further.

## 4. What is still open

- The meaning of the five changed words at stream +3386, +40290, +62446, +62494, +63502 (probably counts of the typed-property section that grow with the baseline fields; no writer traced).
- Whether 0x02401393 is created in manual mode too: yes, the manual-mode run of `drift.ps1` gives the same `114/Props` 41-byte line (see `trace-baseline-r4.md`).
