/*
 * xml.h -- a dependency-free XML reader for MSPDI import.
 *
 * MSPDI files are machine-written: UTF-8, one root, no DTD internal subsets in
 * practice, and no mixed content that matters.  This parser still handles the
 * parts of XML that appear in real exports -- declaration, comments, CDATA,
 * entities, namespace prefixes and the self-closing form -- and rejects
 * mismatched tags rather than guessing.  It is not a general XML processor and
 * does not validate against the schema.
 *
 * The tree keeps direct text separately from child elements, so
 * <Task><Name>a</Name>text</Task> does not smuggle "text" into Name.  Use
 * text_of() for the common "element whose only content is text" case.
 *
 * Namespace prefixes are stripped: mspdi:Task and Task are the same name, and
 * an xmlns declaration is just an attribute.  That is safe for MSPDI, whose
 * schema has no element names that collide once prefixes are removed.
 */

#ifndef CORE_IO_XML_H
#define CORE_IO_XML_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace core::io {

struct XmlAttribute {
    std::string name;   // namespace prefix stripped
    std::string value;  // entities decoded
};

struct XmlElement {
    std::string name;   // namespace prefix stripped
    std::vector<XmlAttribute> attributes;
    std::vector<XmlElement> children;
    std::string text;   // direct character data, concatenated, entities decoded
    std::size_t line = 0;
};

struct XmlParseResult {
    bool ok = false;
    XmlElement root;
    std::string error;
    std::size_t line = 0;

    explicit operator bool() const noexcept { return ok; }
};

/* Parses exactly one document and rejects trailing non-whitespace. */
XmlParseResult parse_xml(std::string_view input);

/* Null when absent.  Names are compared after prefix stripping (as stored). */
const XmlElement* child(const XmlElement& element, std::string_view name) noexcept;

/* All direct children with this name, in document order. */
std::vector<const XmlElement*> children(const XmlElement& element, std::string_view name);

/* nullptr when absent. */
const XmlAttribute* attribute(const XmlElement& element, std::string_view name) noexcept;

/* Direct text with leading/trailing ASCII whitespace removed. */
std::string text_of(const XmlElement& element);

/* True when the element's text is "1" or "true" / "0" or "false". */
bool parse_bool_text(std::string_view text, bool& out) noexcept;

/* Escapes &, <, >, " and ' so the result is safe in text or an attribute. */
std::string escape_xml(std::string_view text);

}  // namespace core::io

#endif  // CORE_IO_XML_H
