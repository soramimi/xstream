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
- `next()`: Move to the next element, returns false when done
- `state()`: Get current state (StartElement, EndElement, Characters, etc.)
- `is_start_element()`, `is_end_element()`, `is_characters()`: Type checking
- `name()`: Get current element name
- `text()`: Get text content of the current element
- `attribute(name)`: Get an attribute value by name
- `attributes()`: Get all attributes
- `path()`: Get current element path

### Writer Class

The `xstream::Writer` class provides:

- `Writer(function)`: Constructor that takes a writer function
- `start_document()`, `end_document()`: Document structure
- `start_element(name)`, `end_element()`: Element handling
- `write_attribute(name, value)`: Add an attribute
- `write_characters(text)`: Add text content
- `element(name, function)`: Create element with lambda for content
- `text_element(name, text)`: Create element with simple text content

## Building and Testing

The project uses the Qt build system with `.pro` files, but the library itself has no dependencies on Qt.

To build the test program:

```bash
cd test
qmake
make
```

## License

This project is provided as-is with no warranty. Use at your own risk.

## Requirements

- C++17 compatible compiler
- No external dependencies for the library itself