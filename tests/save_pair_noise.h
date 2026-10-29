/*
 * save_pair_noise.h -- the W29-1.0b null-save discipline for the save-pair
 * tests.
 *
 * THE PROBLEM IT SOLVES (R28-1 residual)
 *
 * A save pair is a control file and the file Project 2021 saved after ONE
 * edit.  Comparing a stream byte for byte with Project's save is wrong when
 * Project's save also rewrites bytes that have nothing to do with the edit:
 * the comparison fails on per-save noise and the test degrades to a
 * whole-stream expected failure, which cannot tell noise from a real edit
 * effect and would hide one.
 *
 * THE MEASUREMENT
 *
 * For every committed pair W29-1.0b had Project open the CONTROL and save it
 * with NO edit (twice).  The first null save is committed as
 * `<control stem>-null.mpp` and is the mask source:
 *
 *   noise   = a byte that differs between the control and the null save
 *   effect  = a byte that differs between the null save and Project's edit save
 *
 * A noise byte is masked, by a NAMED rule (see the comparison kinds below).
 * An effect byte is NOT masked: the port's save must equal Project's edit save
 * there, byte for byte, or the check FAILS.  A shape whose effects the port
 * cannot reproduce must be refused by the command, not excused by the test.
 *
 * THE MASKS ARE MEASURED, NOT GUESSED
 *
 * WAVE 33 (W33-3.2, audit C-5).  The old fixed-stream rule masked the WHOLE
 * 16-byte block around a control-vs-null difference.  That was wider than the
 * measurement: a coincidence byte that matches control vs null but differs
 * null vs Project could sit inside a noisy block, and a stream whose edit
 * effects all fell inside such blocks passed with effects=0.  The mask is now
 * exactly the control-vs-null difference positions, byte for byte, on every
 * kind:
 *
 * fixed streams (anything of equal length in control and null):
 *   mask byte i iff i < null.size() and control[i] != null[i].  If Project's
 *   edit save changed a byte that the null save leaves equal to the control,
 *   that byte is an EFFECT and the port must reproduce it or the check fails.
 *
 * Variable-length streams:
 *   214/CEdl/Var2Data and Props14 end in a save-path record whose length
 *   changes and whose bytes are the same for every save.  They are normalised
 *   first (see below), then the same per-byte rule applies.  Props14's edit
 *   effect (record 0x3540000a's payload at normalised offset 402) sits next to
 *   noise at 404 and 408..411; 214/CEdl's work-pair effect at 390/420..422 is
 *   likewise in a noisy block.  Both are effects under the narrowed rule.
 *
 * APPENDED TAIL (W33-3.2).  The null save can be shorter than the control (the
 * Props14 "Fixture Builder" region; 38 normalised bytes).  Those positions used
 * to be masked.  They are now compared DIRECTLY against Project's edit save:
 * the port's byte must equal Project's edit save's byte at every appended
 * position, or the check fails.  A tail byte where Project's edit save differs
 * from the control also counts as a tail effect.  `tail_bytes` reports how many
 * positions were compared this way.
 *
 * DocumentSummaryInformation has NO measured noise in any of the pairs
 * (control and null save are byte-identical), so it is compared exactly; a
 * noise byte there fails the "this stream is noise-free" check instead of
 * being masked.
 *
 * DECLARED EFFECTS (W33-3.2).  `compare` takes `effects_declared`: true for a
 * stream the pair test declares the edit changes.  When true, `effects_ok`
 * requires effects + tail_effects > 0, so a stream the edit is measured to
 * change cannot pass with a mask that swallowed the whole change.  A caller
 * that passes true MUST check `verdict.effects_ok` (the tests do).
 *
 * NORMALISATION (measured, not a hash)
 *
 * Props14:  zero the first 8 bytes (size caches), find the unique save-path
 *           record id 08 00 40 35, take the record start as id - 4, and replace
 *           `u32 length / u32 id / u32 kind / payload` with
 *           `u32 8 / id / u32 0 / "PATH" UTF-16LE` (20 bytes).
 *           574 raw -> 484 normalised on the wave-28 controls; the null save
 *           is 38 normalised bytes shorter (Project's null save drops the
 *           "Fixture Builder" marker; that region is now compared directly
 *           against Project's edit save as the appended tail, W33-3.2).
 * 214/CEdl/Var2Data: zero the first 12 bytes, find id 31 00 40 26, same
 *           sentinel replacement.  522 raw -> 430 normalised.
 */

#ifndef SAVE_PAIR_NOISE_H
#define SAVE_PAIR_NOISE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace save_pair {

using Bytes = std::vector<std::uint8_t>;

/* How the pair test must compare one stream. */
enum class Kind {
    /* Equal-length stream: per-byte mask from control vs null. */
    fixed,
    /* DocumentSummaryInformation: zero measured noise, exact comparison. */
    exact,
    /* Props14: normalise, then per-byte mask. */
    props14,
    /* 214/CEdl/Var2Data: normalise, then per-byte mask. */
    cedl,
};

struct Verdict {
    /* Bytes of the mask source: control vs null. */
    std::size_t noise = 0;
    /* Positions beyond the null save's length, compared directly against
     * Project's edit save instead of masked (W33-3.2).  Measured: the Props14
     * "Fixture Builder" region, and 214/CEdl on no pair. */
    std::size_t tail_bytes = 0;
    /* Unmasked bytes where Project's edit save differs from the null save:
     * the edit's effects.  The port must reproduce every one. */
    std::size_t effects = 0;
    /* Appended-tail bytes where Project's edit save differs from the control:
     * a change Project made in the region its null save dropped. */
    std::size_t tail_effects = 0;
    /* Unmasked bytes (including the appended tail) where the port differs
     * from Project's edit save. */
    std::size_t mismatches = 0;
    /* For a non-exact stream: control and null have the same length. */
    bool lengths_ok = true;
    /* For `exact`: control and null are byte-identical (no noise). */
    bool noise_free = true;
    /* The caller declared this stream changes with the edit. */
    bool effects_declared = false;
    /* A declared stream has a non-empty measured effect set.  W33-3.2: a
     * stream the pair means to change must not pass with 0 effects. */
    bool effects_ok = true;
    /* First mismatch, for the failure message. */
    std::size_t first_mismatch = static_cast<std::size_t>(-1);
    std::uint8_t output_byte = 0;
    std::uint8_t result_byte = 0;
};

/* The committed null save for a pair control: `<stem>-null.mpp`. */
std::string null_fixture_name(std::string const& control_name);

/* Normalise the two variable-length streams.  A stream without the record id is
 * returned with only its size caches zeroed. */
Bytes normalise_props14(Bytes const& raw);
Bytes normalise_cedl(Bytes const& raw);

/*
 * Compare one stream under the discipline.
 *
 * `control` is the pair's control file stream, `null_save` the committed
 * no-edit save of that control, `result` Project's edit save and `output` the
 * port's save.  `effects_declared` says the pair test declares this stream
 * changes with the edit; the caller must then check `effects_ok`.
 */
Verdict compare(Kind kind, Bytes const& control, Bytes const& null_save,
                Bytes const& result, Bytes const& output,
                bool effects_declared = false);

/* ---- implementation ---------------------------------------------------- */

inline std::uint32_t le32(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline void append_u32(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

inline std::string null_fixture_name(std::string const& control_name) {
    std::string stem = control_name;
    std::string const suffix = ".mpp";
    if (stem.size() >= suffix.size() &&
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
        stem.resize(stem.size() - suffix.size());
    }
    return stem + "-null.mpp";
}

/* Replace the save-path record with a same-length-everywhere sentinel. */
inline Bytes normalise_record(Bytes const& raw, std::size_t head,
                              std::uint8_t id0, std::uint8_t id1,
                              std::uint8_t id2, std::uint8_t id3) {
    Bytes out = raw;
    for (std::size_t i = 0; i < head && i < out.size(); ++i) {
        out[i] = 0;
    }
    for (std::size_t at = 0; at + 4 <= raw.size(); ++at) {
        if (raw[at] != id0 || raw[at + 1] != id1 || raw[at + 2] != id2 ||
            raw[at + 3] != id3) {
            continue;
        }
        if (at < 4) {
            return out;
        }
        std::size_t const record = at - 4;
        if (record + 12 > raw.size()) {
            return out;
        }
        std::uint32_t const length = le32(raw, record);
        if (record + 12 + length > raw.size()) {
            return out;
        }
        Bytes sentinel;
        append_u32(sentinel, 8);
        sentinel.insert(sentinel.end(), raw.begin() +
                                             static_cast<std::ptrdiff_t>(record + 4),
                        raw.begin() +
                            static_cast<std::ptrdiff_t>(record + 8));
        append_u32(sentinel, 0);
        for (char const letter : std::string("PATH")) {
            sentinel.push_back(static_cast<std::uint8_t>(letter));
            sentinel.push_back(0);
        }
        out.erase(out.begin() + static_cast<std::ptrdiff_t>(record),
                  out.begin() +
                      static_cast<std::ptrdiff_t>(record + 12 + length));
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(record),
                   sentinel.begin(), sentinel.end());
        return out;
    }
    return out;
}

inline Bytes normalise_props14(Bytes const& raw) {
    return normalise_record(raw, 8, 0x08, 0x00, 0x40, 0x35);
}

inline Bytes normalise_cedl(Bytes const& raw) {
    return normalise_record(raw, 12, 0x31, 0x00, 0x40, 0x26);
}

/*
 * W33-3.2 MEASURED OPEN GAP.  Narrowing the fixed-stream mask exposed a byte
 * in the two 214 sub-streams that the old 16-byte block mask hid: Project's
 * editor save resets ONE flag byte there, and the port copies the control's
 * byte because the writers do not own these streams.  Measured signatures
 * (port / Project, and the offsets are stream offsets, not hashes):
 *
 *   214/CEdl/VarMeta +21      0x02 / 0x01   (five actuals pairs, three outline)
 *   214/Props +11403          0x02 / 0x01   (five actuals pairs, three outline)
 *   214/Props +8902           0x31 / 0x43   (one actuals pair)
 *
 * These are recorded as NAMED expected failures in the pair tests that carry
 * the streams (actuals and outline, app and core; the task-mode pair has no
 * 214 stream) and under "Found, not fixed" in the wave-33 record; the mask is
 * NOT widened back.  `open` is the exact measured mismatch, `fixed` is the same single
 * effect with the port reproducing it (the expected-failure entry must then
 * be deleted, so the caller must fail on `fixed`), and `none` is everything
 * else -- any other mismatch stays a hard failure.
 */
enum class GapState { none, open, fixed };

inline bool path_ends_with(std::string const& path, char const* suffix) {
    std::string const tail(suffix);
    return path.size() >= tail.size() &&
           path.compare(path.size() - tail.size(), tail.size(), tail) == 0;
}

inline GapState open_gap_state(std::string const& path,
                               Verdict const& verdict) {
    if (verdict.effects != 1) {
        return GapState::none;
    }
    std::size_t offset = 0;
    std::uint8_t port_byte = 0;
    std::uint8_t project_byte = 0;
    if (path_ends_with(path, "214/CEdl/VarMeta")) {
        offset = 21;
        port_byte = 0x02;
        project_byte = 0x01;
    } else if (path_ends_with(path, "214/Props")) {
        offset = verdict.first_mismatch == 8902 ? 8902 : 11403;
        port_byte = offset == 8902 ? 0x31 : 0x02;
        project_byte = offset == 8902 ? 0x43 : 0x01;
    } else {
        return GapState::none;
    }
    if (verdict.mismatches == 0) {
        return GapState::fixed;
    }
    if (verdict.mismatches == 1 && verdict.first_mismatch == offset &&
        verdict.output_byte == port_byte &&
        verdict.result_byte == project_byte) {
        return GapState::open;
    }
    return GapState::none;
}

/*
 * The Props14 edit marker's payload as edit_marker.cpp reads it: 0 or 1, or -1
 * when the record is absent (a hand-built fixture, where the writer is a
 * no-op) or ambiguous.  W33-3.2: a pair declares Props14 changed only when the
 * control still stores 0.  A control that an earlier edit produced already
 * stores 1, Project's next save leaves the payload alone, and a zero effect
 * count there is correct rather than a gap.
 */
inline long props14_marker_payload(Bytes const& raw) {
    constexpr std::uint8_t kId[4] = {0x0A, 0x00, 0x40, 0x35};
    std::size_t at = 0;
    bool once = false;
    for (std::size_t i = 0; i + 4 <= raw.size(); ++i) {
        if (raw[i] != kId[0] || raw[i + 1] != kId[1] || raw[i + 2] != kId[2] ||
            raw[i + 3] != kId[3]) {
            continue;
        }
        if (once) {
            return -1;
        }
        once = true;
        at = i;
    }
    if (!once || at < 4) {
        return -1;
    }
    std::size_t const record = at - 4;
    if (record + 14 > raw.size() || le32(raw, record) != 2 ||
        le32(raw, record + 4) != 0x3540000Au || le32(raw, record + 8) != 2) {
        return -1;
    }
    return static_cast<long>(raw[record + 12]) |
           (static_cast<long>(raw[record + 13]) << 8);
}

inline Verdict compare(Kind kind, Bytes const& control,
                       Bytes const& null_save, Bytes const& result,
                       Bytes const& output, bool effects_declared) {
    Verdict verdict;
    verdict.effects_declared = effects_declared;
    Bytes control_bytes = control;
    Bytes null_bytes = null_save;
    Bytes result_bytes = result;
    Bytes output_bytes = output;
    std::vector<bool> mask;
    if (kind == Kind::props14 || kind == Kind::cedl) {
        Bytes (*normalise)(Bytes const&) =
            kind == Kind::props14 ? normalise_props14 : normalise_cedl;
        control_bytes = normalise(control);
        null_bytes = normalise(null_save);
        result_bytes = normalise(result);
        output_bytes = normalise(output);
        verdict.lengths_ok = control_bytes.size() == result_bytes.size() &&
                             control_bytes.size() == output_bytes.size();
    } else if (kind == Kind::exact) {
        verdict.noise_free = control_bytes == null_bytes;
        verdict.lengths_ok = true;
    } else {
        verdict.lengths_ok = control_bytes.size() == null_bytes.size();
    }

    /* W33-3.2: the mask is exactly the control-vs-null difference positions.
     * The old 16-byte block mask (fixed streams) and the masked tail were
     * wider than the measurement and could hide an effect. */
    mask.assign(control_bytes.size(), false);
    for (std::size_t i = 0; i < control_bytes.size(); ++i) {
        if (i < null_bytes.size() && control_bytes[i] != null_bytes[i]) {
            mask[i] = true;
        }
    }
    if (control_bytes.size() > null_bytes.size()) {
        verdict.tail_bytes = control_bytes.size() - null_bytes.size();
    }

    /* Count each control-vs-null word once: `noise` is the mask source. */
    std::size_t const common = std::min(control_bytes.size(), null_bytes.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (control_bytes[i] != null_bytes[i]) {
            ++verdict.noise;
        }
    }

    std::size_t const span = std::max(
        std::max(result_bytes.size(), output_bytes.size()),
        control_bytes.size());
    for (std::size_t i = 0; i < span; ++i) {
        bool const masked = i < mask.size() && mask[i];
        /* Positions beyond the null save are the appended tail: Project's own
         * null save dropped them, so they are compared with Project's edit
         * save directly instead of being counted as null-vs-result effects. */
        bool const in_tail = i >= null_bytes.size();
        if (!masked && !in_tail) {
            bool const result_differs =
                i >= result_bytes.size() || null_bytes[i] != result_bytes[i];
            if (result_differs) {
                ++verdict.effects;
            }
        } else if (!masked && in_tail) {
            bool const tail_differs =
                i >= control_bytes.size() || i >= result_bytes.size() ||
                control_bytes[i] != result_bytes[i];
            if (tail_differs) {
                ++verdict.tail_effects;
            }
        }
        if (masked) {
            continue;
        }
        bool const output_differs =
            i >= output_bytes.size() || i >= result_bytes.size() ||
            output_bytes[i] != result_bytes[i];
        if (output_differs) {
            if (verdict.mismatches == 0) {
                verdict.first_mismatch = i;
                verdict.output_byte = i < output_bytes.size()
                                          ? output_bytes[i]
                                          : 0;
                verdict.result_byte = i < result_bytes.size()
                                          ? result_bytes[i]
                                          : 0;
            }
            ++verdict.mismatches;
        }
    }
    verdict.effects_ok =
        !verdict.effects_declared ||
        verdict.effects + verdict.tail_effects > 0;
    return verdict;
}

} /* namespace save_pair */

#endif /* SAVE_PAIR_NOISE_H */
