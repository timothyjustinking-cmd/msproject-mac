/*
 * mpp_field_value_tests.cpp -- W48: the custom-field read API.
 *
 * CLAIM UNDER TEST.  `mpp::field_value.h`'s id table is the PjTask one from
 * analysis/field_id_map.tsv, and `mpp::Task::custom_fields` /
 * `mpp::Document::custom_field(uid, id)` decode the measured storage:
 *
 *   Text1     0x0b400033  VarMeta block, UTF-16LE + NUL   ("alpha")
 *   Number1/2 0x0b400057/58  VarMeta block, 8-byte f64    (42.5 / 3.25)
 *   Duration1 0x0b400067  VarMeta block, u32 file ticks   (4800 = 1 day, COM 480 min)
 *   Date1     0x0b400109  VarMeta block, packed u32       (0x3d1812c0 = 2026-10-26 08:00)
 *   Flag1     0x0b400048  Task FixedMeta entry +24 bit 0x02 (NOT VarMeta)
 *
 * The measured inputs are Project's own files: corpus/probe/w10-fieldvalues.mpp
 * (five values on T1), w10-flag-off.mpp and w10-flag-on.mpp (Flag1 False/True,
 * no other difference).  They are git-ignored, so the corpus cases SKIP on a
 * clean checkout; the field-id range checks and the committed
 * tests/data/synthetic-base.mpp control always run.
 *
 * This test reads files only; it writes nothing.
 */

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "field_value.h"
#include "reader.h"

#ifndef MPP_PROBE_DIR
#define MPP_PROBE_DIR "corpus/probe"
#endif
#ifndef MPP_TEST_DATA_DIR
#define MPP_TEST_DATA_DIR "tests/data"
#endif

namespace {

class TestContext {
  public:
    void check(bool condition, std::string const& what) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cout << "FAIL: " << what << "\n";
        }
    }
    void equal(std::uint32_t actual, std::uint32_t expected,
               std::string const& what) {
        check(actual == expected, what + " (actual " + std::to_string(actual) +
                                      ", expected " + std::to_string(expected) +
                                      ")");
    }
    void equal(std::size_t actual, std::size_t expected, std::string const& what) {
        check(actual == expected, what + " (actual " + std::to_string(actual) +
                                      ", expected " + std::to_string(expected) +
                                      ")");
    }
    int report() const {
        std::cout << checks_ << " checks, " << failures_ << " failures\n";
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::string join(std::string const& a, std::string const& b) { return a + "/" + b; }

/* ---- (a) the id table ---------------------------------------------------- */

void test_id_table(TestContext& t) {
    using mpp::FieldKind;
    /* Text: 1..6 stride 3, 7..10 contiguous, then 11..30 contiguous. */
    t.equal(mpp::custom_field_id(FieldKind::text, 1), 0x0B400033u, "Text1");
    t.equal(mpp::custom_field_id(FieldKind::text, 2), 0x0B400036u, "Text2");
    t.equal(mpp::custom_field_id(FieldKind::text, 6), 0x0B400042u, "Text6");
    t.equal(mpp::custom_field_id(FieldKind::text, 7), 0x0B400043u, "Text7");
    t.equal(mpp::custom_field_id(FieldKind::text, 10), 0x0B400046u, "Text10");
    t.equal(mpp::custom_field_id(FieldKind::text, 11), 0x0B40013Du, "Text11");
    t.equal(mpp::custom_field_id(FieldKind::text, 30), 0x0B400150u, "Text30");
    t.equal(mpp::custom_field_id(FieldKind::text, 31), 0u, "Text31 is out of range");
    /* Flag: 1..10, then 11..20 at 0x124. */
    t.equal(mpp::custom_field_id(FieldKind::flag, 1), 0x0B400048u, "Flag1");
    t.equal(mpp::custom_field_id(FieldKind::flag, 10), 0x0B400051u, "Flag10");
    t.equal(mpp::custom_field_id(FieldKind::flag, 11), 0x0B400124u, "Flag11");
    t.equal(mpp::custom_field_id(FieldKind::flag, 20), 0x0B40012Du, "Flag20");
    /* Number: 1..5, then 6..20 at 0x12e. */
    t.equal(mpp::custom_field_id(FieldKind::number, 1), 0x0B400057u, "Number1");
    t.equal(mpp::custom_field_id(FieldKind::number, 5), 0x0B40005Bu, "Number5");
    t.equal(mpp::custom_field_id(FieldKind::number, 6), 0x0B40012Eu, "Number6");
    t.equal(mpp::custom_field_id(FieldKind::number, 20), 0x0B40013Cu, "Number20");
    /* Duration: 1..3, then 4..10 at 0x113. */
    t.equal(mpp::custom_field_id(FieldKind::duration, 1), 0x0B400067u, "Duration1");
    t.equal(mpp::custom_field_id(FieldKind::duration, 3), 0x0B400069u, "Duration3");
    t.equal(mpp::custom_field_id(FieldKind::duration, 4), 0x0B400113u, "Duration4");
    t.equal(mpp::custom_field_id(FieldKind::duration, 10), 0x0B400119u, "Duration10");
    /* Cost: 1..3, then 4..10 at 0x102. */
    t.equal(mpp::custom_field_id(FieldKind::cost, 1), 0x0B40006Au, "Cost1");
    t.equal(mpp::custom_field_id(FieldKind::cost, 4), 0x0B400102u, "Cost4");
    t.equal(mpp::custom_field_id(FieldKind::cost, 10), 0x0B400108u, "Cost10");
    /* Date: 1..10 contiguous from 0x109. */
    t.equal(mpp::custom_field_id(FieldKind::date, 1), 0x0B400109u, "Date1");
    t.equal(mpp::custom_field_id(FieldKind::date, 10), 0x0B400112u, "Date10");

    /* Kind/index round trip for every id the four measured families own. */
    std::size_t round_trips = 0;
    for (std::uint32_t id = 0x0B400000u; id <= 0x0B400160u; ++id) {
        FieldKind const kind = mpp::custom_field_kind(id);
        if (kind == FieldKind::unknown) {
            continue;
        }
        unsigned const index = mpp::custom_field_index(id);
        t.check(index != 0, "non-unknown kind with index 0 for id " +
                                std::to_string(id));
        t.equal(mpp::custom_field_id(kind, index), id, "id -> kind/index -> id");
        ++round_trips;
    }
    /* 30 text + 20 flag + 20 number + 10 duration + 10 cost + 10 date. */
    t.equal(round_trips, std::size_t{100}, "100 ids in the six families");

    /* Name 0x0b40000e and the 0x0b4004xx/0x0b4005xx blocks are NOT custom fields. */
    t.check(mpp::custom_field_kind(0x0B40000Eu) == FieldKind::unknown,
            "Name is not a custom field");
    t.check(mpp::custom_field_kind(0x0B400505u) == FieldKind::unknown,
            "StartText is not a custom field");

    /* Storage-measured predicate: Cost1..Cost10 are measured (W60); Flag2..20
     * are not. */
    t.check(mpp::custom_field_storage_measured(FieldKind::flag, 1),
            "Flag1's bit is measured");
    t.check(!mpp::custom_field_storage_measured(FieldKind::flag, 2),
            "Flag2's bit is unmeasured");
    t.check(mpp::custom_field_storage_measured(FieldKind::cost, 1),
            "Cost1's storage is measured (W60)");
    t.check(mpp::custom_field_storage_measured(FieldKind::cost, 10),
            "Cost10's storage is measured (W60)");
    t.check(mpp::custom_field_storage_measured(FieldKind::number, 20),
            "Number20's storage is measured");
}

/* ---- (b) the committed synthetic control (no corpus) --------------------- */

void test_synthetic_control(TestContext& t) {
    std::string const path = join(MPP_TEST_DATA_DIR, "synthetic-base.mpp");
    if (!file_exists(path)) {
        std::cout << "SKIP: " << path << " is missing\n";
        return;
    }
    mpp::Document const document = mpp::load_file(path);
    t.equal(document.tasks.size(), std::size_t{3}, "synthetic: three live tasks");
    for (mpp::Task const& task : document.tasks) {
        /* Every live row has a FixedMeta entry, so Flag1 is a stored False; no
         * other custom field is stored in this file. */
        t.equal(task.custom_fields.size(), std::size_t{1},
                "synthetic uid " + std::to_string(task.uid) +
                    ": only Flag1 is stored");
        mpp::CustomFieldValue const* flag = task.custom_field(0x0B400048u);
        t.check(flag != nullptr, "synthetic: Flag1 is pseudo-stored (clear bit)");
        if (flag != nullptr) {
            t.check(flag->present && flag->storage_measured && !flag->flag,
                    "synthetic: Flag1 reads False");
        }
        t.check(task.custom_field(0x0B400033u) == nullptr,
                "synthetic: no Text1 is stored");
        mpp::CustomFieldValue const text = document.custom_field(task.uid, 0x0B400033u);
        t.check(!text.present, "synthetic: Document::custom_field Text1 not present");
        t.equal(text.field_id, 0x0B400033u, "Document::custom_field echoes the id");
        t.check(text.kind == mpp::FieldKind::text, "Document::custom_field kind Text");
    }
    /* A Cost lookup is known, its storage is measured (W60), but this file
     * stores no value. */
    mpp::CustomFieldValue const cost = document.custom_field(1, 0x0B40006Au);
    t.check(!cost.present, "synthetic: Cost1 not present");
    t.check(cost.storage_measured,
            "synthetic: Cost1's storage encoding is measured (absent value)");
    t.check(cost.kind == mpp::FieldKind::cost, "synthetic: Cost1 kind Cost");
}

/* ---- (c) Project's measured field-values file --------------------------- */

void test_field_values(TestContext& t) {
    std::string const path = join(MPP_PROBE_DIR, "w10-fieldvalues.mpp");
    if (!file_exists(path)) {
        std::cout << "SKIP: " << path << " is missing (corpus/ is git-ignored)\n";
        return;
    }
    mpp::Document const document = mpp::load_file(path);
    t.equal(document.tasks.size(), std::size_t{2},
            "w10-fieldvalues: two live tasks");
    mpp::Task const* t1 = nullptr;
    mpp::Task const* t2 = nullptr;
    for (mpp::Task const& task : document.tasks) {
        if (task.uid == 1) {
            t1 = &task;
        } else if (task.uid == 2) {
            t2 = &task;
        }
    }
    t.check(t1 != nullptr && t2 != nullptr, "w10-fieldvalues: uids 1 and 2");

    mpp::CustomFieldValue const* text = t1->custom_field(0x0B400033u);
    t.check(text != nullptr && text->present && text->storage_measured,
            "uid 1: Text1 is present and decodable");
    if (text != nullptr) {
        t.check(text->kind == mpp::FieldKind::text, "uid 1: Text1 kind");
        t.check(text->text == "alpha", "uid 1: Text1 decodes as the measured 'alpha'");
    }
    mpp::CustomFieldValue const* number1 = t1->custom_field(0x0B400057u);
    t.check(number1 != nullptr && number1->present, "uid 1: Number1 present");
    if (number1 != nullptr) {
        t.check(number1->number == 42.5, "uid 1: Number1 is 42.5");
    }
    mpp::CustomFieldValue const* number2 = t1->custom_field(0x0B400058u);
    t.check(number2 != nullptr && number2->present && number2->number == 3.25,
            "uid 1: Number2 is 3.25");
    mpp::CustomFieldValue const* duration = t1->custom_field(0x0B400067u);
    t.check(duration != nullptr && duration->present, "uid 1: Duration1 present");
    if (duration != nullptr) {
        t.equal(duration->duration_ticks, 4800u,
                "uid 1: Duration1 is 4800 file ticks (one 8-hour day)");
        t.equal(duration->duration_minutes(), 480u,
                "uid 1: Duration1 is 480 COM minutes");
    }
    mpp::CustomFieldValue const* date = t1->custom_field(0x0B400109u);
    t.check(date != nullptr && date->present, "uid 1: Date1 present");
    if (date != nullptr) {
        t.equal(date->packed_date, 0x3D1812C0u,
                "uid 1: Date1 is the measured packed word 2026-10-26 08:00");
    }
    mpp::CustomFieldValue const* flag = t1->custom_field(0x0B400048u);
    t.check(flag != nullptr && flag->present && flag->storage_measured &&
                flag->flag,
            "uid 1: Flag1 is stored True in the FixedMeta tail");

    /* T2 has no custom values at all except the stored-clear Flag1. */
    t.equal(t2->custom_fields.size(), std::size_t{1},
            "uid 2: only Flag1 is stored");
    t.check(t2->custom_field(0x0B400033u) == nullptr, "uid 2: no Text1");
    mpp::CustomFieldValue const* flag2 = t2->custom_field(0x0B400048u);
    t.check(flag2 != nullptr && !flag2->flag, "uid 2: Flag1 is False");

    /* Document-level lookup. */
    mpp::CustomFieldValue const via_document = document.custom_field(1, 0x0B400057u);
    t.check(via_document.present && via_document.number == 42.5,
            "Document::custom_field(1, Number1) is 42.5");
    mpp::CustomFieldValue const absent = document.custom_field(1, 0x0B400058u + 97u);
    t.check(!absent.present, "an unknown id is not present");
    t.check(absent.kind == mpp::FieldKind::unknown, "an unknown id has kind unknown");
}

/* ---- (d) the W60 cost fixtures: the byte is seen, the reader lane --------- */

void test_cost_fixtures(TestContext& t) {
    std::string const path = join(MPP_PROBE_DIR, "w14-cost.mpp");
    if (!file_exists(path)) {
        std::cout << "SKIP: " << path << " is missing (corpus/ is git-ignored)\n";
        return;
    }
    mpp::Document const doc = mpp::load_file(path);
    mpp::CustomFieldValue const cost = doc.custom_field(1, 0x0B40006Au);
    t.check(cost.present, "w14-cost: Cost1's VarMeta block is present");
    t.check(cost.kind == mpp::FieldKind::cost, "w14-cost: Cost1 kind Cost");
    if (cost.storage_measured) {
        t.check(cost.cost == 123.45, "w14-cost: Cost1 decodes as 123.45");
    } else {
        std::cout << "NOTE: core/mpp does not decode a Cost block yet (the "
                     "reader lane); the writer proves the encoding\n";
    }

    std::string const path2 = join(MPP_PROBE_DIR, "w14-cost2.mpp");
    if (!file_exists(path2)) {
        return;
    }
    mpp::Document const doc2 = mpp::load_file(path2);
    mpp::CustomFieldValue const first = doc2.custom_field(1, 0x0B40006Au);
    mpp::CustomFieldValue const second = doc2.custom_field(2, 0x0B40006Au);
    t.check(first.present && second.present,
            "w14-cost2: Cost1 present on uids 1 and 2");
    if (first.storage_measured && second.storage_measured) {
        t.check(first.cost == 1.23, "w14-cost2: uid 1 Cost1 is 1.23");
        t.check(second.cost == 0.07, "w14-cost2: uid 2 Cost1 is 0.07");
    }
}

/* ---- (e) the Flag1 discriminator: Flag1 False vs True, nothing else ------ */
void test_flag_pair(TestContext& t) {
    std::string const off = join(MPP_PROBE_DIR, "w10-flag-off.mpp");
    std::string const on = join(MPP_PROBE_DIR, "w10-flag-on.mpp");
    if (!file_exists(off) || !file_exists(on)) {
        std::cout << "SKIP: " << off << " / " << on
                  << " are missing (corpus/ is git-ignored)\n";
        return;
    }
    mpp::Document const a = mpp::load_file(off);
    mpp::Document const b = mpp::load_file(on);
    t.equal(a.tasks.size(), std::size_t{1}, "flag-off: one live task");
    t.equal(b.tasks.size(), std::size_t{1}, "flag-on: one live task");
    mpp::CustomFieldValue const* off_flag = a.tasks.front().custom_field(0x0B400048u);
    mpp::CustomFieldValue const* on_flag = b.tasks.front().custom_field(0x0B400048u);
    t.check(off_flag != nullptr && !off_flag->flag, "flag-off: Flag1 is False");
    t.check(on_flag != nullptr && on_flag->flag, "flag-on: Flag1 is True");
    /* The pair differs only in Flag1, so every other value must be absent on
     * both sides -- the bit is the whole store. */
    t.equal(a.tasks.front().custom_fields.size(), std::size_t{1},
            "flag-off: exactly one custom field");
    t.equal(b.tasks.front().custom_fields.size(), std::size_t{1},
            "flag-on: exactly one custom field");
}

} /* namespace */

int main() {
    TestContext t;
    test_id_table(t);
    test_synthetic_control(t);
    test_field_values(t);
    test_cost_fixtures(t);
    test_flag_pair(t);
    return t.report();
}
