# YAML

This implementation is a YAML 1.2.2 compliant parser. In addition to parsing YAML, we also provide the capability to output YAML event files, JSON, and canonically formatted YAML.

We validate our implementation against the official [YAML test suite](https://github.com/yaml/yaml-test-suite).

## Getting Started

To use Trieste YAML in your own codebase you will need to configure your CMAKE project with the `TRIESTE_BUILD_PARSERS` flag set. Trieste YAML definitions are in the `trieste::yaml` namespace, and to access them you need to include the `trieste/yaml.h` header. The YAML `Reader` will emit a tree that adheres to the following well-formedness definition (copied here from the header file):

```c++
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

Everything present in the input stream is exposed via an AST with this definition. You can obtain such a node using the `reader()` function:

```c++
ProcessResult result = yaml::reader().file(input_path).read();
```

See [the documentation]() for more information on how to use a `Reader` object.

We also expose the following helpers:
- `event_writer` a `Writer` object which emits a YAML event file
- `

