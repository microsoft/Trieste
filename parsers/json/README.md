# JSON

The files in this directory provide a [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259) compliant JSON language implementation using Trieste. It is able to both read and write JSON files.

We are grateful to the maintainers of the [JSONTestSuite](https://github.com/nst/JSONTestSuite), which we use to ensure compliance with RFC 8259.

## Getting Started

To use Trieste JSON in your own codebase you will need to configure your CMAKE project with the `TRIESTE_BUILD_PARSERS` flag set. Trieste JSON definitions are in the `trieste::json` namespace, and to access them you need to include the `trieste/json.h` header. The JSON implementation is based around the following well-formedness definition (copied here from the header file):

```cpp
inline const auto wf_value_tokens =
  Object | Array | String | Number | True | False | Null;

// clang-format off
inline const auto wf =
  (Top <<= wf_value_tokens++[1])
  | (Object <<= Member++)
  | (Member <<= String * (Value >>= wf_value_tokens))
  | (Array <<= wf_value_tokens++)
  ;
// clang-format on
```

The language implementation exposes the following helpers:

- `reader()` - `Reader` that parses any valid JSON file and produces an AST that conforms to `json::wf`. Optionally, this reader will read non-compliant JSON files which contain more than one JSON value at the top level of the file.
- `writer()` - `Writer` that takes a JSON AST that conforms to `json::wf` and produces a JSON file.

## Creating JSON Nodes

The public API exposes several methods to help create JSON documents:

```cpp
Node object = json::object(
    {json::member("key_a_str", "value"),
     json::member("key_b_number", 42),
     json::member("key_c_bool", json::boolean(true)),
     json::member("key_d_null", json::null()),
     json::member("key_e_array", json::array({json::value(1), json::value(2)})),
     json::member(
       "key_f_object",
       json::object({json::member("key", json::value("value"))}))});

std::cout << json::to_string(object) << std::endl;
// {"key_a_str":"value","key_b_number":42,"key_c_bool":true,"key_d_null":null,"key_e_array":[1,2],"key_f_object":{"key":"value"}}

Nodes elements;
elements.push_back(json::value(1));
elements.push_back(json::value("two"));
elements.push_back(json::boolean(false));
elements.push_back(json::null());
Node array = json::array(elements.begin(), elements.end());
```

## Reading values
There are also methods to get C++ values back out of nodes, such as:

- `optional<double> get_number(const Node&)`

   Attempts to get a number out of a `Node`. If the node is not of type `Number`, or cannot be parsed as a double, returns `nullopt`.
- `optional<bool> get_boolean(const Node&)`

   Attempts to get a boolean value out of a `Node`. If the node is not of type `True` or `False`, returns `nullopt`.
- `optional<Location> get_string(const Node&)`

   Attempts to get a string out of a `Node`. This will remove the double quotation marks. If the node is not of type `String`, returns `nullopt`.

You can also use a [JSON Pointer](https://www.rfc-editor.org/rfc/rfc6901) to select nodes out of a document using the `select()` function:

```cpp
std::cout << "c: " << json::select(object, {"/key_c_bool"}).value();
// c: (json-true)

std::cout << "a: " << json::select_string(object, {"/key_a_str"}).value() << std::endl;
// a: value

std::cout << "e1[1]: " << json::select_number(object, {"/key_e_array/1"}).value() << std::endl;
// e[1]: 2

std::cout << "missingkey: " << json::select(object, {"/missingkey"});
// missingkey: missing key: (error (errormsg 42:Member does not exist with key: missingkey) (errorast ... )
```

Note the `select_<type>` functions which mirror those above and perform a select and then a `get_<type>` call.

## Json Patch
The `patch(const Node&, const Location&)` function provides support for [JSON Patch](https://www.rfc-editor.org/rfc/rfc6902).
The implementation is fully compliant with RFC 6902 and we thank the maintainers of the
[JSON Patch Test Suite](https://github.com/json-patch/json-patch-tests), which we have integrated with our CI and pass in full.

```cpp
auto reader = json::reader();
auto doc =
      reader.synthetic(R"json({"foo": {"bar": {"baz": [{"boo": "net"}]}}})json")
        .read()
        .ast->front();
auto patch = reader
                .synthetic(R"json([
    {"op": "copy", "from": "/foo", "path": "/bak"},
    {"op": "replace", "path": "/foo/bar/baz/0/boo", "value": "qux"}
  ])json")
                .read()
                .ast->front();

auto patched = json::patch(doc, patch);
std::cout << "patched: " << json::to_string(patched) << std::endl;
// patched: {"foo":{"bar":{"baz":[{"boo":"qux"}]}},"bak":{"bar":{"baz":[{"boo":"net"}]}}}
