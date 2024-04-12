# JSON

The files in this directory provide a [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259) compliant JSON language implementation using Trieste. It is able to both read and write JSON files.

We are grateful to the maintainers of the [JSONTestSuite](https://github.com/nst/JSONTestSuite), which we use to ensure compliance with RFC 8259.

## Getting Started

To use Trieste JSON in your own codebase you will need to configure your CMAKE project with the `TRIESTE_BUILD_PARSERS` flag set. Trieste JSON definitions are in the `trieste::json` namespace, and to access them you need to include the `trieste/json.h` header. The JSON implementation is based around the following well-formedness definition (copied here from the header file):

```c++
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
