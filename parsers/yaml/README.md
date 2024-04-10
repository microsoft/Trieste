# YAML

The files in this directory provide a [YAML 1.2.2](https://yaml.org/spec/1.2.2/) compliant language implementation using Trieste. In addition to parsing YAML, we also provide the capability to output YAML event files, JSON, and canonically formatted YAML.

We validate our implementation against the official [YAML test suite](https://github.com/yaml/yaml-test-suite).

## Getting Started

To use Trieste YAML in your own codebase you will need to configure your CMake project with the `TRIESTE_BUILD_PARSERS` flag set. Trieste YAML definitions are in the `trieste::yaml` namespace, and to access them you need to include the `trieste/yaml.h` header. The YAML implementation is based around the following well-formedness definition (copied here from the header file):

```c++
inline const auto wf_tokens = Mapping | Sequence | Value | Int | Float |
  True | False | Hex | Null | SingleQuote | DoubleQuote | Plain |
  AnchorValue | Alias | TagValue | Literal | Folded | Empty | FlowMapping |
  FlowSequence;

inline const auto wf_flow_tokens = wf_tokens - (Mapping | Sequence);

inline const auto wf =
  (Top <<= Stream)
  | (Stream <<= Directives * Documents)
  | (Documents <<= Document++)
  | (Document <<= Directives * DocumentStart * (Value >>= wf_tokens) * DocumentEnd)
  | (Directives <<= (TagDirective | VersionDirective | UnknownDirective)++)
  | (TagDirective <<= TagPrefix * TagHandle)[TagPrefix]
  | (Mapping <<= MappingItem++[1])
  | (MappingItem <<= (Key >>= wf_tokens) * (Value >>= wf_tokens))
  | (FlowMapping <<= FlowMappingItem++)
  | (FlowMappingItem <<= (Key >>= wf_flow_tokens) * (Value >>= wf_flow_tokens))
  | (AnchorValue <<= Anchor * (Value >>= wf_tokens))[Anchor]
  | (TagValue <<= TagPrefix * TagName * (Value >>= wf_tokens))
  | (Sequence <<= wf_tokens++[1])
  | (FlowSequence <<= wf_flow_tokens++)
  | (SingleQuote <<= (BlockLine|EmptyLine)++[1])
  | (DoubleQuote <<= (BlockLine|EmptyLine)++[1])
  | (Literal <<= AbsoluteIndent * ChompIndicator * Lines)
  | (Folded <<= AbsoluteIndent * ChompIndicator * Lines)
  | (Lines <<= (BlockLine|EmptyLine)++)
  | (Plain <<= (BlockLine|EmptyLine)++[1])
  ;
```

The language implementation exposes the following helper constructs:

- `reader()` - `Reader` that parses any valid 1.2.2 YAML file and produces an AST that conforms to `yaml::wf`.
- `writer()` - `Writer` that takes a YAML AST that conforms to `yaml::wf` and produces a YAML file.
- `event_writer()` - `Writer` that takes a YAML AST and produces a YAML event file.
- `to_json` - `Rewriter` that takes a YAML AST and converts it to a JSON AST that conforms to `json::wf`.
