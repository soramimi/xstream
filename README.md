[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/soramimi/xstream)

# Xstream

A lightweight, header-only XML parsing and generation library for C++.

## Overview

Xstream is a simple XML parser and generator that provides a clean interface for working with XML data in C++ applications. It offers both reading (parsing) and writing (generating) capabilities with minimal dependencies.

### Features

- Header-only library - just include `xstream.h` and start using it
- XML parsing with support for elements, attributes, CDATA, and comments
- XML generation with proper indentation and formatting
- HTML entity encoding and decoding
- Path-based element navigation
- Modern C++ design using C++17 features (string_view, optional)
- No external dependencies

## Usage

### Reading XML

```cpp
#include "xstream.h"

void parse_example() {
    std::string xml = "<root><item id='1'>Hello, <![CDATA[world]]></item></root>";
    
    xstream::Reader reader(xml);
    while (reader.next()) {
        if (reader.is_start_element()) {
            std::string name = reader.name();
            printf("Start element: %s\n", name.c_str());
            
            // Access attributes
            auto id_attr = reader.attribute("id");
            if (id_attr) {
                printf("  Attribute id: %s\n", std::string(*id_attr).c_str());
            }
        } 
        else if (reader.is_end_element()) {
            printf("End element: %s\n", reader.name().c_str());
            printf("  Text content: %s\n", reader.text().c_str());
        } 
        else if (reader.is_characters()) {
            auto chars = reader.characters().decode();
            printf("Characters: %s\n", std::string(chars.data(), chars.size()).c_str());
        }
    }
}
```

### Input and result lifetimes

The constructors that take a `string_view`, null-terminated character pointer,
or pointer range borrow the XML data. Keep that data alive and unchanged until
parsing has finished. String literals, character pointers, `std::string` lvalues,
and `string_view` remain supported, including the overloads with `Limits`.
Constructing a reader directly from a temporary `std::string` (including a const
temporary or `std::move(string)`) is a compile-time error:

```cpp
std::string xml = "<root/>";
xstream::Reader reader(xml); // xml must outlive parsing.
// xstream::Reader invalid(std::string("<root/>")); // Deleted constructor.
```

This restriction cannot detect dangling views or pointers supplied explicitly.
For example, a `string_view` constructed from a temporary string does not extend
the string's lifetime; do not store that view in a reader. In streaming mode,
`input()` copies each chunk, so the caller can release or reuse it after the call.

Element names, attributes, and accumulated text are stored independently of the
streaming input buffer. Values returned by `name()`, `text()`, `attribute()`,
`attributes()`, and `characters()` own their data and remain valid after more
input is supplied or the reader is reset. References returned by `path()` refer
to the reader's current state and should not be retained across parsing calls.

With `parse(callback)`, `next()` requests more input and resumes parsing until
it can return a complete event. Refilling the buffer does not produce an event.
If the callback supplies no additional data, `next()` returns false and
`is_not_enough_input()` returns true; the caller can supply more input and resume.
Self-closing elements produce one start event followed by one end event, after
the complete `/>` has been received.

Closing tags must exactly match the innermost open element, including case and
any namespace prefix. Mismatched or extra closing tags, and self-closing end tags
such as `</root/>`, stop parsing: `next()` returns false and
`state()` returns `xstream::Reader::Error`. The invalid end tag is not returned
as an event. This error persists across subsequent calls; no further input is
requested, and `input()` is ignored until `parse()` resets the reader. A closing
tag split across chunks is checked only after its closing `>` arrives.

### Comment syntax

By default, comments reject `--` inside the body and a body ending in `-`,
as required by XML. To accept these nonstandard comments, enable the flag before
parsing any bytes (also supported with streaming input):

```cpp
xstream::Reader reader("<root><!--a--b---></root>");
reader.set_allow_invalid_comments(true); // Default: false.
while (reader.next()) {
    // Handle events.
}
```

`allow_invalid_comments()` returns the current flag. Configure it outside the
input callback, before the first bytes are parsed; changing it later produces
`InvalidOperation`. `parse(callback)` preserves the flag and permits configuring
it again before parsing the new document. The flag only relaxes these two hyphen
restrictions: the first `-->` always ends a comment, comments do not nest, and
unterminated comments still produce `UnexpectedEof` at EOF. Resource limits apply
in both modes. Other XML processors may reject comments accepted by this flag.

In the default mode, `--` at the end of an input chunk waits for the next byte:
`>` completes the comment, while any other byte produces `InvalidSyntax` at that
byte, before publishing the comment event. EOF while waiting is `UnexpectedEof`.

### Attributes and declarations

Each parsed attribute list must have unique names, including the lists exposed
on XML declaration and processing-instruction events. Names are compared exactly,
including case and any namespace prefix. The same name can be reused on a
different element. Attribute order is preserved. A duplicate produces
`InvalidSyntax` at the first byte of the second occurrence of the name; the
complete tag is checked before publishing its start or declaration event.

XML declarations (`<?xml ...?>`) and processing instructions (`<?target ...?>`)
remain available as `Declaration` events through the existing attribute-oriented
interface. This interface does not implement arbitrary processing-instruction
content. `DOCTYPE`, entity declarations, and other `<!...>` markup declarations
are rejected with `UnsupportedDeclaration`. Comments and CDATA sections remain
supported. DTD parsing, declared-entity expansion, and external resource resolution
are not performed. Rejection happens as soon as the byte after `<!` distinguishes
an unsupported declaration from a comment or CDATA prefix, without waiting for
the declaration to finish.

### Character references, Unicode, and encoding

The reader accepts UTF-8 input and applies the character and name ranges from
[XML 1.0 (Fifth Edition)](https://www.w3.org/TR/REC-xml/#sec-common-syn).
Malformed UTF-8 (including overlong forms, surrogates, and values above U+10FFFF)
and characters outside XML's `Char` range are rejected in every parsing context,
including comments and CDATA. Element, attribute, and processing-instruction
names are checked by code point. Names are compared exactly, without Unicode
normalization or case folding; namespace prefixes are not resolved to URIs.

Text and parsed attribute values support the five predefined entities (`amp`,
`lt`, `gt`, `quot`, `apos`), decimal references such as `&#128512;`, and hexadecimal
references such as `&#x1F600;`. Both numeric forms produce UTF-8, including
characters above U+FFFF. The hexadecimal prefix is lowercase `x`; hexadecimal
digits may use either case. A semicolon is required. Unknown entities, malformed
numbers, overflow, and references to forbidden XML characters produce
`InvalidCharacterReference`. References are decoded once. They remain literal
in comments and CDATA. As with the existing interface, parsed pseudoattribute
values on processing-instruction events use the same reference handling.

Validation happens during parsing, before returning an affected event, even if
the application never calls a text or attribute accessor. A partial reference
waits for its semicolon, the end of its text/value, or EOF. A partial UTF-8
sequence waits for more bytes; an invalid sequence, including truncation at EOF,
produces `InvalidUtf8`. All split positions have the same result and byte offset
as one-shot input. Resource limits continue to count raw input bytes.

An optional UTF-8 BOM is skipped only at byte zero; it is still included in
document sizes and diagnostic offsets. XML `encoding` declarations accept
`UTF-8` case-insensitively. Other encoding names and UTF-16 BOMs produce
`UnsupportedEncoding`; no transcoding is attempted. An encoding name with
invalid syntax produces `InvalidSyntax`.

The standalone `html_decode()` and `html_encode()` use these strict UTF-8 and XML
character checks. Invalid input is reported through a return value and optional
`ErrorInfo`, without an input-validation exception. With the encoder's
`utf8through` argument set to false, Unicode characters become numeric references
to code points rather than references to individual UTF-8 bytes. Writer text and
attribute escaping use this encoder.

### Return values and error state

Use the checked overloads when empty output and conversion failure must be
distinguished:

```cpp
std::string output;
xstream::ErrorInfo error;
if (!xstream::html_decode("&#x1F600;", &output, &error)) {
    fprintf(stderr, "Conversion failed at byte %zu: %s\n",
            error.offset, error.message.c_str());
}
if (!xstream::html_encode(output, &output, &error, false)) {
    // Encoding failed; output still contains its previous value.
}
```

Both functions return `true` on success and replace the output. On failure they
return `false`, leave the output unchanged, and set the supplied diagnostic.
Output parameters use pointers rather than non-const references; pass `&output`.
A null output pointer returns `false` with `InvalidOperation` at offset zero.
The diagnostic pointer can be null. Const references remain available for
read-only inputs and accessors; mutable parameters use pointers throughout the
implementation and test helpers.
Encoding/decoding in place is supported. Success clears an earlier diagnostic.
The diagnostic pointer is optional; `InvalidUtf8`, `InvalidCharacter`, and
`InvalidCharacterReference` use the same meanings as Reader errors, with offsets
relative to the conversion's input. Output size overflow reports `LimitExceeded`.

The existing string-returning overloads remain available. They now return an
empty string on failure and can also fill a diagnostic:

```cpp
auto text = xstream::html_decode(input, &error);
auto escaped = xstream::html_encode(input, &error); // UTF-8 passes through.
auto numeric = xstream::html_encode(input, false, &error);
```

An empty string alone cannot distinguish failure from valid empty input; use the
checked overload or inspect `error.code`. Previously these conversion failures
threw `std::invalid_argument`. Reader validates input before exposing values,
so existing value-returning accessors continue to work for valid events.
The owned character part also offers `decode(&vector, &error)`, and attribute
values offer `to_string(&string, &error)` for checked conversion.

Reader retains its `ReadResult` and first-error state API. `Reader::ErrorCode`
and `Reader::ErrorInfo` remain available as aliases of the shared namespace
types. The library header contains no explicit `throw`, `try`, or `catch`.
Allocation failures from standard containers and exceptions raised by user
callbacks can still propagate when exceptions are enabled; they are not mapped
to XML input errors. RAII restores callback/advancing guards during unwinding.
With exceptions disabled, an allocation failure is not recoverable through
these status APIs. The standalone integration check below verifies normal and
invalid-input paths with `-fno-exceptions`.

These checks do not establish complete XML conformance: namespace processing,
full XML declaration/processing-instruction grammar, line-ending normalization,
and attribute-value normalization are not implemented. DTD declarations remain
explicitly unsupported.

### Completion and errors

`advance()` reports progress separately from the event type returned by `state()`.
`next()` remains available and returns true exactly when `advance()` would return
`ReadResult::Event`. After it returns false, inspect `result()`:

| `Reader::ReadResult` | Meaning |
| --- | --- |
| `Event` | A complete event is available through the existing accessors |
| `NeedInput` | Parsing can resume when more bytes or an EOF notification arrive |
| `EndDocument` | EOF was reached, the root is closed, and all input was consumed |
| `SubtreeEnd` | A `nest()` scope ended; its boundary event is held for the next call |
| `Error` | Parsing stopped; `error()` contains the first error |

The pointer and `string_view` constructors treat their range as the complete
document. A default-constructed reader and `parse(callback)` accept streaming
input. Call `finish_input()` when the source reaches EOF, including from inside
the callback. Neither an empty `input()` call nor a callback returning without
data means EOF. A closed root alone does not finish a streaming document.

```cpp
std::string_view xml = "<root><item>text</item></root>";
size_t pos = 0;
xstream::Reader reader;
reader.parse([&] {
    if (pos < xml.size()) {
        reader.input(xml.substr(pos++, 1));
    } else {
        reader.finish_input();
    }
});

while (reader.next()) {
    if (reader.is_end_element()) {
        printf("%s: %s\n", reader.path().c_str(), reader.text().c_str());
    }
}
if (reader.result() == xstream::Reader::ReadResult::Error) {
    auto const& error = reader.error();
    fprintf(stderr, "XML error at byte %zu: %s\n",
            error.offset, error.message.c_str());
}
```

`error()` returns an `ErrorInfo` containing `code`, `offset`, and `message`.
Offsets count bytes from zero across the entire input, including after buffer
compaction. `UnexpectedEof` points just past the last received byte;
`MismatchedEndTag` points to the invalid end tag's `<`; `InvalidSyntax` points to
the offending byte. `InputError` uses the end of the received input, while
`InvalidOperation` uses the parser's current position. `UnsupportedDeclaration`
points to the unsupported declaration's `<`. `None` means no error.

| Character/encoding error code | Byte offset |
| --- | --- |
| `InvalidCharacterReference` | The reference's `&`, including an incomplete reference at EOF |
| `InvalidUtf8` | The first byte of the invalid or truncated UTF-8 sequence |
| `InvalidCharacter` | The first UTF-8 byte of a forbidden literal XML character |
| `UnsupportedEncoding` | The first byte of the XML encoding value, or byte zero for a UTF-16 BOM |

EOF inside a tag, quoted value, comment, CDATA section, or an unclosed element
produces `UnexpectedEof`, unless a pending reference or UTF-8 sequence gives the
more specific error above. Once an error is detected, parsing fails without
requesting more data. Basic structural checks require one root, quoted attribute
values, and no non-whitespace text outside the root. An empty document is an
`UnexpectedEof` error. These structural and character checks cover the supported
subset of XML described above.

Input callbacks can call `input()`, `finish_input()`, or `input_error(message)`.
Use `input_error()` to report an I/O failure instead of signaling normal EOF.
Reentering parsing, resetting the reader, or calling `nest()` or `hold()` from
the callback produces `InvalidOperation`. Allocation failures and exceptions
thrown by the callback propagate to the caller; they are not converted into
syntax errors.

Repeated calls after `EndDocument` or `Error` retain that result and do not invoke
the callback. Repeating `finish_input()` is safe. Supplying nonempty input after
EOF produces `InvalidOperation`; operations after an error preserve its first
diagnostic. `parse(callback)` resets the result, error, input, and nesting state.
The error reference is valid until the reader is reset or its diagnostic changes.

### Resource limits and incremental input

Every reader uses finite default limits. Configure them before supplying input,
or pass them with a borrowed document:

```cpp
xstream::Reader::Limits limits;
limits.max_depth = 128;
limits.max_buffer_bytes = 64 * 1024;
limits.max_document_bytes = 32 * 1024 * 1024;
xstream::Reader streaming(limits);
// streaming.parse(callback) preserves these limits.

std::string_view xml = "<root/>";
xstream::Reader borrowed(xml, limits);
```

| `Reader::Limits` field | Default | What is counted |
| --- | --- | --- |
| `max_depth` | 256 | Open elements, with the root at depth 1; declarations do not add an element level. Also bounds `nest()` scopes |
| `max_name_bytes` | 1 KiB | An element, end-tag, attribute, or declaration name; declaration names include `?` or `!` |
| `max_attributes` | 256 | Attributes on one element or declaration |
| `max_attribute_value_bytes` | 1 MiB | One raw, encoded attribute value |
| `max_token_bytes` | 4 MiB | One text segment or markup token, including markup delimiters |
| `max_buffer_bytes` | 8 MiB | Streaming ring capacity and unprocessed input plus an incoming chunk |
| `max_element_text_bytes` | 8 MiB | Raw text and CDATA retained by one element, excluding child-element text |
| `max_total_text_bytes` | 16 MiB | Raw text and CDATA accumulated across open elements |
| `max_text_parts` | 65,536 | Accumulated element text/CDATA fragments, including empty CDATA sections |
| `max_total_attribute_bytes` | 8 MiB | Raw attribute names and values retained across open elements and the current declaration |
| `max_path_bytes` | 64 KiB | The current event path, including `/` separators |
| `max_document_bytes` | 64 MiB | All bytes supplied for the document, including consumed input |

The boundary itself is allowed; zero means no allowance, not unlimited. These
are byte counts, not Unicode character counts. `set_limits()` is available on
an empty streaming reader before input; changing limits after input or from an
input callback produces `InvalidOperation`. `limits()` exposes the configuration.
`parse()` preserves limits while clearing parsing state, counters, and errors.

An exceeded limit produces `ErrorCode::LimitExceeded`, an explanatory message,
and a byte offset. Oversized chunks are rejected before any part of the chunk is
copied. Token, name, value, and retention limits are checked while scanning,
including incomplete tokens, before allocating their event values. The first
error remains sticky as described above. Text and attribute allowances are
released when their element is retired. Whitespace inside an element also counts
as retained text. Comments retain no body data or fragments. Separate fragment
and attribute-count limits bound metadata even for empty values.

Whitespace before and after the root is published as `Characters` events without
being accumulated as element text. For these events, `characters()` returns the
current whitespace and `text()` returns an empty string. The reader owns at most
one such event payload, bounded by `max_token_bytes` and the string container's
maximum size, independently of the element-text retention limits. Advancing to
parse another event or resetting with `parse()` releases that payload; `hold()`
can replay the current event. Copies returned by `characters()` remain valid.
The event payload is excluded from `retained_text_bytes` and
`retained_text_parts`, so outside whitespace is accepted even when the text
retention limits are zero. Document and streaming-buffer limits still apply,
and non-whitespace text outside the root remains an error.

The streaming buffer limit does not apply to a borrowed document: no streaming
buffer is allocated for it. Document, token, depth, name, and retention limits
still apply. One `input()` call must fit alongside all unprocessed bytes, so split
large sources into appropriately sized chunks. Budgets describe payload sizes
and object counts; container bookkeeping and temporary storage during growth
are additional. Caller-owned input and copies returned by accessors are not
included in retention accounting.

Scanning resumes from the saved lexical state, including quotes, attributes,
and partial comment/CDATA terminators. Repeated calls without new input do not
rescan the unfinished token. The ring grows geometrically up to its configured
limit and reuses consumed space without moving the live suffix. Element stacks
store names and previous path lengths rather than complete path prefixes.

`statistics()` returns saturating counters for `scan_steps` (byte inspections),
`copied_input_bytes` (copies into or during growth of the streaming buffer), and
`appended_path_bytes` (new path bytes). It also reports current `buffer_capacity`,
`buffered_bytes`, `retained_text_bytes`, `retained_text_parts`,
`retained_attribute_bytes`, and `path_bytes`. The retained text counters count
only accumulated element text, excluding the current outside-root whitespace
event described above. These counters support deterministic work and retention
checks without relying on wall-clock timings.

### Writing XML

```cpp
#include "xstream.h"
#include <string>
#include <vector>

void write_example() {
    std::vector<char> output;
    
    auto writer_fn = [&output](const char* data, int size) -> int {
        output.insert(output.end(), data, data + size);
        return size;
    };
    
    xstream::Writer writer(writer_fn);
    
    writer.start_document();
    
    writer.start_element("root");
    writer.start_element("item");
    writer.write_attribute("id", "1");
    writer.write_characters("Hello, world!");
    writer.end_element();  // Close item
    
    // Alternative syntax using lambda
    writer.element("item", [&](){
        writer.write_attribute("id", "2");
        writer.write_characters("Another item");
    });
    
    // Simple text element
    writer.text_element("simple", "Text content");
    
    writer.end_element();  // Close root
    writer.end_document();
    
    // output vector now contains the formatted XML
    std::string result(output.begin(), output.end());
    printf("%s\n", result.c_str());
}
```

## API Reference

### Reader Class

The `xstream::Reader` class provides the following key methods:

- `Reader(string_view)`: Constructor that takes XML data
- `parse(callback)`: Reset for streaming input; the callback supplies data or signals EOF
- `input(chunk)`, `finish_input()`, `input_error(message)`: Supply bytes or report source completion/failure
- `advance()`, `result()`: Advance and inspect the progress result
- `next()`: Advance to an event; false can mean waiting, subtree end, document end, or error
- `error()`: Get the first error's code, byte offset, and message
- `Limits`, `set_limits()`, `limits()`: Configure and inspect resource limits before input
- `statistics()`: Inspect work counters and current retained payload sizes
- `state()`: Get current state (StartElement, EndElement, Characters, etc.)
- `is_start_element()`, `is_end_element()`, `is_characters()`: Type checking
- `name()`: Get current element name
- `text()`: Get accumulated text of the current element; empty outside the root
- `attribute(name)`: Get an attribute value by name
- `attributes()`: Get all attributes
- `path()`: Get current element path

### Writer Class

Writer operations return `bool`. Check each return value, or check `good()` and
`error()` after a sequence of calls. The first failure remains stored; later
operations return `false` without sending further output. Create a new Writer
for a new document after an error.

Invalid text/attribute values report `InvalidUtf8` or `InvalidCharacter` with an
offset relative to that value. Values are encoded before modifying the pending
tag/text buffer. An attribute outside an open start tag or a missing output
callback reports `InvalidOperation`. This does not add full validation of
Writer-generated names or document structure.

The default indent width is 4 spaces. `set_indent_width(width)` accepts widths
from 0 through `Writer::max_indent_width` (64), and `indent_width()` returns the
current value. Configure it before the first writing operation, including
`start_document()` or an operation that buffers data or produces no bytes.
Repeated changes before writing are allowed. An out-of-range width, a change
after writing has started, or a change from the output callback reports
`InvalidOperation`, preserves the width, and stops further output. Its offset is
the number of bytes accepted before the failure. Indentation size calculations
are checked before allocation; an overflow or container size limit reports
`LimitExceeded`.

Width 0 disables all automatic formatting: indentation, the newline after the
XML declaration, and the final document newline. Whitespace supplied as text
or attribute values is preserved. In formatted mode, calling `write_characters()`
(including with an empty string) suppresses subsequent automatic whitespace
between children and before the closing tag of that element. This state belongs
to each element: a child's own element-only content can still be formatted, and
closing a child does not forget its parent's text state.

Formatting already emitted before the first text cannot be removed. For example,
writing a child before its parent's first text can already have inserted a
newline and indentation. Set width 0 before writing arbitrary mixed content to
preserve all text; the writer does not buffer an entire parent to predict later
text. Element-only formatting keeps the existing default output.

Attribute values escape tab, newline, and carriage return as `&#9;`, `&#10;`, and
`&#13;`, respectively, so XML attribute-value normalization does not change them
in other parsers. Ordinary text and the public `html_encode()` keep literal tabs
and newlines and escape carriage return as before. Reader itself does not
implement attribute-value normalization.

The output callback returns the number of bytes accepted. Positive partial
writes are retried with the remaining bytes. Zero, a negative value, or a count
larger than requested reports `OutputError`; its offset is the number of bytes
accepted before the failure. `output_error(message)` lets a callback provide a
custom diagnostic. Reentering a writing operation from the output callback is
rejected with `InvalidOperation`. Exceptions from the callback itself are not
caught or converted; recovery after a callback has partially written and thrown
is the caller's responsibility.

```cpp
xstream::Writer writer([&](char const *p, int n) { return sink_write(p, n); });
if (!writer.set_indent_width(0) || !writer.start_document() || !writer.start_element("root") ||
    !writer.write_characters(text) || !writer.end_document()) {
    fprintf(stderr, "Write failed at byte %zu: %s\n",
            writer.error().offset, writer.error().message.c_str());
}
```

The class provides:

- `Writer(function)`: Constructor that takes a writer function
- `set_indent_width(width)`, `indent_width()`: Set formatting before writing; 0 disables it, default 4, maximum `max_indent_width` (64)
- `start_document()`, `end_document()`: Document structure
- `start_element(name)`, `end_element()`: Element handling
- `write_attribute(name, value)`: Add an attribute
- `write_characters(text)`: Add text content
- `element(name, function)`: Create element with lambda for content
- `text_element(name, text)`: Create element with simple text content
- `good()`, `error()`: Inspect the first error; `Writer::ErrorCode`/`ErrorInfo` alias the shared types
- `output_error(message)`: Report a sink failure from the callback

## Building and Testing

The project uses the Qt build system with `.pro` files, but the library itself has no dependencies on Qt.

To build the test program:

```bash
cd test
qmake
make
```

The existing XML examples check event order/counts and successful completion in
addition to their value assertions. The `Fuzzing.*` tests exercise the shared
differential and resource-limit checks with deterministic mutations.
For optional coverage-guided fuzzing, seed replay, timeout checks, and failure
reproduction, see [Reader fuzzing](test/fuzz/README.md).

The library can also be exercised without compiler exception support, separately
from GoogleTest's exception-related tests:

```bash
g++ -std=c++17 -DNDEBUG -O2 -fno-exceptions -Wall -Wextra -Werror \
    -Iinclude test/no_exceptions/main.cpp -o /tmp/xstream-no-exceptions
/tmp/xstream-no-exceptions
```

## License

This project is provided as-is with no warranty. Use at your own risk.

## Requirements

- C++17 compatible compiler
- No external dependencies for the library itself
