#include "core_c.h"

#include "field_table.h"

extern "C" {

size_t core_field_count(void) { return core::model::field_count(); }

const core_field_info* core_field_at(size_t index) {
    return core::model::field_at(index);
}

const core_field_info* core_field_by_id(uint32_t field_id) {
    return core::model::find_field_by_id(field_id);
}

const core_field_info* core_field_by_name(const char* name) {
    return core::model::find_field_by_name(name);
}

const core_field_info* core_field_by_name_in(const char* name, uint8_t record_type) {
    return core::model::find_field_by_name(
        name, static_cast<core::model::RecordType>(record_type));
}

const char* core_record_type_name(uint8_t record_type) {
    return core::model::record_type_name(
        static_cast<core::model::RecordType>(record_type));
}

const char* core_c_api_version(void) { return "core-field-table/1"; }

}  // extern "C"
