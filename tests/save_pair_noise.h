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
 * fixed streams (anything of equal length in control and null):
 *   A control-vs-null difference masks the whole 16-byte block that contains
 *   it.  The per-save values are 16-byte identity GUIDs and 16-byte cache
 *   records, so a per-byte mask is wrong in the other direction: at
 *   214/CEdl/VarMeta +21 and 214/Props +11403 / +8902 a coincidence byte
 *   matches control vs null but differs null vs Project, and it sits inside a
 *   record whose other bytes are noise.  Measured on all 23 pairs.
 *
 * Variable-length streams:
 *   214/CEdl/Var2Data and Props14 end in a save-path record whose length
 *   changes and whose bytes are the same for every save.  They are normalised
 *   first (see below), then a PER-BYTE mask is used: mask a byte when the
 *   control and null differ there, or when the null save is shorter.  Props14
 *   needs the per-byte mask because the edit effect (record 0x3540000a's
 *   payload at normalised offset 402) sits in the same 16-byte block as noise
 *   at 404 and 408..411; a block mask would hide the effect.  214/CEdl's
 *   work-pair effect at 390/420..422 is likewise in a noisy block.
 *
 * DocumentSummaryInformation has NO measured noise in any of the 23 pairs
 * (control and null save are byte-identical), so it is compared exactly; a
 * noise byte there fails the "this stream is noise-free" check instead of
 * being masked.
 *
 * NORMALISATION (measured, not a hash)
 *
 * Props14:  zero the first 8 bytes (size caches), find the unique save-path
 *           record id 08 00 40 35, take the record start as id - 4, and replace
 *           `u32 length / u32 id / u32 kind / payload` with
 *           `u32 8 / id / u32 0 / "PATH" UTF-16LE` (20 bytes).
 *           574 raw -> 484 normalised on the wave-28 controls; the null save
 *           is 38 bytes shorter (Project's null save drops the "Fixture
 *           Builder" marker; that whole region is masked as beyond the null
 *           save's length).
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
    /* Equal-length stream: 16-byte block mask from control vs null. */
    fixed_block,
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
    /* Bytes masked because the null save is shorter (measured: the Props14
     * "Fixture Builder" region, and 214/CEdl on no pair). */
    std::size_t masked_tail = 0;
    /* Unmasked bytes where Project's edit save differs from the null save:
     * the edit's effects.  The port must reproduce every one. */
    std::size_t effects = 0;
    /* Unmasked bytes where the port differs from Project's edit save. */
    std::size_t mismatches = 0;
    /* For a non-exact stream: control and null have the same length. */
    bool lengths_ok = true;
    /* For `exact`: control and null are byte-identical (no noise). */
    bool noise_free = true;
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
 * port's save.
 */
Verdict compare(Kind kind, Bytes const& control, Bytes const& null_save,
                Bytes const& result, Bytes const& output);

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

inline Verdict compare(Kind kind, Bytes const& control,
                       Bytes const& null_save, Bytes const& result,
                       Bytes const& output) {
    Verdict verdict;
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
        mask.assign(control_bytes.size(), false);
        for (std::size_t i = 0; i < control_bytes.size(); ++i) {
            mask[i] = i >= null_bytes.size() ||
                      control_bytes[i] != null_bytes[i];
        }
        verdict.lengths_ok = control_bytes.size() == result_bytes.size() &&
                             control_bytes.size() == output_bytes.size();
        verdict.masked_tail =
            control_bytes.size() > null_bytes.size()
                ? control_bytes.size() - null_bytes.size()
                : 0;
    } else if (kind == Kind::exact) {
        verdict.noise_free = control_bytes == null_bytes;
        verdict.lengths_ok = true;
    } else {
        mask.assign(control_bytes.size(), false);
        for (std::size_t i = 0; i < control_bytes.size(); ++i) {
            if (i >= null_bytes.size() || control_bytes[i] != null_bytes[i]) {
                std::size_t const start = (i / 16) * 16;
                std::size_t const stop =
                    std::min(start + 16, control_bytes.size());
                for (std::size_t j = start; j < stop; ++j) {
                    mask[j] = true;
                }
            }
        }
        verdict.lengths_ok = control_bytes.size() == null_bytes.size();
        verdict.masked_tail =
            control_bytes.size() > null_bytes.size()
                ? control_bytes.size() - null_bytes.size()
                : 0;
    }

    std::size_t const common = std::min(control_bytes.size(), null_bytes.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (control_bytes[i] != null_bytes[i]) {
            ++verdict.noise;
        }
    }

    std::size_t const span =
        std::max(result_bytes.size(), output_bytes.size());
    for (std::size_t i = 0; i < span; ++i) {
        bool const masked = i < mask.size() && mask[i];
        if (masked) {
            continue;
        }
        bool const result_differs =
            i >= result_bytes.size() || i >= null_bytes.size() ||
            null_bytes[i] != result_bytes[i];
        if (result_differs) {
            ++verdict.effects;
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
    return verdict;
}

} /* namespace save_pair */

#endif /* SAVE_PAIR_NOISE_H */
