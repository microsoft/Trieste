// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "trieste/trieste.h"

namespace trieste
{
  namespace json
  {
    using namespace wf::ops;

    inline const auto Value = TokenDef("json-value");
    inline const auto Object = TokenDef("json-object", flag::symtab);
    inline const auto Array = TokenDef("json-array");
    inline const auto String = TokenDef("json-string", flag::print);
    inline const auto Number = TokenDef("json-number", flag::print);
    inline const auto True = TokenDef("json-true");
    inline const auto False = TokenDef("json-false");
    inline const auto Null = TokenDef("json-null");
    inline const auto Member = TokenDef("json-member", flag::lookdown);
    inline const auto ErrorSeq = TokenDef("json-errorseq");
    inline const auto Key = TokenDef("json-key", flag::print);

    // groups
    inline const auto ArrayGroup = TokenDef("json-array-group");
    inline const auto ObjectGroup = TokenDef("json-object-group");

    inline const auto wf_value_tokens =
      Object | Array | String | Number | True | False | Null;

    // clang-format off
    inline const auto wf =
      (Top <<= wf_value_tokens++[1])
      | (Object <<= Member++)
      | (Member <<= Key * (Value >>= wf_value_tokens))[Key]
      | (Array <<= wf_value_tokens++)
      | (Error <<= ErrorMsg * ErrorAst)
      | (ErrorSeq <<= Error++)
      ;
    // clang-format on

    /// @brief Reads JSON documents into a JSON AST.
    /// @param allow_multiple Whether to allow multiple top-level JSON documents
    /// in a file
    /// @return A Trieste reader
    Reader reader(bool allow_multiple = false);

    /// @brief Writes JSON ASTs to an output file.
    /// @details The arguments here are the same as those of json::to_string(),
    /// and have the same effect.
    /// @param path the relative path of the JSON document
    /// @param prettyprint whether to output the JSON with newlines and tab
    /// spacing to make it more human-readable
    /// @param sort_keys whether to sort the keys of the objects lexigraphically
    /// @param indent the indent to use when prettyprinting the JSON
    /// @return a Trieste writer
    Writer writer(
      const std::filesystem::path& path,
      bool prettyprint = false,
      bool sort_keys = false,
      const std::string& indent = "  ");

    /// @brief Returns a JSON string generated from the provided JSON AST.
    /// @param path the relative path of the JSON document
    /// @param prettyprint whether to output the JSON with newlines and tab
    /// spacing to make it more human-readable
    /// @param sort_keys whether to sort the keys of the objects lexigraphically
    /// @param indent the indent to use when prettyprinting the JSON
    /// @return a JSON string
    std::string to_string(
      Node json,
      bool prettyprint = false,
      bool sort_keys = false,
      const std::string& indent = "  ");

    /// @brief Tests whether two JSON objects are equal
    /// @details This test is done in-place and is less computationally
    /// intensive than generating two strings and comparing them.
    /// @param lhs_json The first JSON AST
    /// @param rhs_json The second JSON AST
    /// @return whether the two JSON ASTs are equivalent
    bool equal(Node lhs_json, Node rhs_json);

    /// @brief Unescapes a JSON string.
    /// @details The argument has been encoded so that it is a valid JSON
    /// string. The result will be the same string but with any escaped elements
    /// re-encoded as raw UTF-8.
    /// @param string The escaped JSON string
    /// @return the unescaped string
    std::string unescape(const std::string_view& string);

    /// @brief Escapes any non-ASCII characters in the string.
    /// @details This method uses JSON unicode escaping to escape any Unicode
    /// characters which have values of `0x7FFF` or less, as supported by the
    /// standard. Any Unicode characters above that limit are replaced with a
    /// Unicode `BAD (0xFFFD)` value.
    /// @param string A UTF-8 encoded string
    /// @return an ASCII string
    std::string escape_unicode(const std::string_view& string);

    /// @brief Escapes any invalid JSON characters in a string.
    /// @details Several characters are invalid in JSON strings and must be
    /// escaped, as per the standard. This method will return a valid JSON UTF-8
    /// string.
    /// @param string the string to escape
    /// @return a valid UTF-8 encoded JSON string.
    std::string escape(const std::string_view& string);

    /// @brief Builds a JSON `Object` node from a list of `Member` nodes.
    /// @param members `Member` nodes used to construct the `Object`
    /// @return an `Object` node
    Node object(const Nodes& members);

    /// @brief Builds a JSON `Member` node from a key and a value.
    /// @param key the key of the member
    /// @param value the value of the member
    /// @return a `Member` node
    Node member(Node key, Node value);

    /// @brief Builds a JSON `Array` node from a list of elements.
    /// @param elements the elements of the array
    /// @return an `Array` node
    Node array(const Nodes& elements);

    /// @brief Builds a JSON `String` node from a string value.
    /// @param value the string value
    /// @return a `String` node
    Node value(const std::string& value);

    /// @brief Builds a JSON `Number` node from a double value.
    /// @param value the double value
    /// @return a `Number` node
    Node value(double value);

    /// @brief Builds a JSON `Boolean` node from a boolean value.
    /// @param value the boolean value
    /// @return a `Boolean` node
    Node boolean(bool value);

    /// @brief Builds a JSON `Null` node.
    /// @return a `Null` node
    Node null();

    /// @brief Retrieves the value of a JSON `Number` node.
    /// @param value the `Number` node
    /// @return the numeric value, or an empty optional if the node is not a
    /// `Number`
    std::optional<double> get_number(const Node& value);

    /// @brief Retrieves the value of a JSON `Boolean` node.
    /// @param value the `Boolean` node
    /// @return the boolean value, or an empty optional if the node is not a
    /// `Boolean`
    std::optional<bool> get_boolean(const Node& value);

    /// @brief Retrieves the value of a JSON `String` node.
    /// @param value the `String` node
    /// @return the string value, or an empty optional if the node is not a
    /// `String`
    std::optional<Location> get_string(const Node& value);

    /// @brief Selects a JSON node from a document using a pointer.
    /// @param document the JSON document
    /// @param pointer the pointer to the node, adhering to RFC 6901
    /// @return the selected node, or `Error` if the node could not be found
    Node select(const Node& document, const Location& pointer);

    /// @brief Selects a JSON `Number` node from a document using a pointer.
    /// @param document the JSON document
    /// @param pointer the pointer to the node, adhering to RFC 6901
    /// @return the numeric value, or an empty optional if the node is not a
    /// `Number`
    std::optional<double>
    select_number(const Node& document, const Location& pointer);

    /// @brief Selects a JSON `Boolean` node from a document using a pointer.
    /// @param document the JSON document
    /// @param pointer the pointer to the node, adhering to RFC 6901
    /// @return the boolean value, or an empty optional if the node is not a
    /// `Boolean`
    std::optional<bool>
    select_boolean(const Node& document, const Location& pointer);

    /// @brief Selects a JSON `String` node from a document using a pointer.
    /// @param document the JSON document
    /// @param pointer the pointer to the node, adhering to RFC 6901
    /// @return the string value, or an empty optional if the node is not a
    /// `String`
    std::optional<Location>
    select_string(const Node& document, const Location& pointer);

    /// @brief Applies a JSON Patch (RFC 6902) to a JSON document.
    /// @param document the JSON document to patch
    /// @param patch the JSON Patch to apply
    /// @return the patched JSON document, or `json::Error` if the patch could
    /// not be applied
    Node patch(const Node& document, const Node& patch);
  }
}
