# Parsers

In addition to providing the Trieste library, we have also provided reference parsers for JSON and YAML as practical examples. You (the language implementer) can use these language implementions as templates and guides for how to implement your own language toolchain using Trieste. In this document we will highlight the key features we believe a language implementation should have. You can learn more about implementing helper classes in the [`infix` tutorial](../samples/infix/README.md).

## WF Definition

Each of the language implementations exposes various things in their respective namespaces. For example, the JSON implementation exposes this WF definition at `trieste::json::wf`:

```c++
inline const auto Object = TokenDef("json-object");
inline const auto Array = TokenDef("json-array");
inline const auto String = TokenDef("json-string", flag::print);
inline const auto Number = TokenDef("json-number", flag::print);
inline const auto True = TokenDef("json-true");
inline const auto False = TokenDef("json-false");
inline const auto Null = TokenDef("json-null");
inline const auto Member = TokenDef("json-member");

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

This is the well-formedness definition which corresponds to the AST of a successfully parsed JSON document. There are two best practices here to keep in mind. The first is the C++ namespace. As a rule, it is a good idea for your language implementation to provide its own namespace, in which the tokens, WF definitions, and helper constructs will be exposed to your users. The second is the token prefixing, *i.e.*:

```c++
inline const auto Object = TokenDef("json-object");
```

Tokens in Trieste must have unique names, and so using a prefix like `json-` ensures that the tokens used do not collide with tokens defined by other language implementations which may be at use in the project.

## Reader

Every language implementation should expose a `Reader` helper in its namespace. For example, here is the one for JSON:

```c++
Reader reader(bool allow_multiple = false);
```

Any parsing settings should be passed to this method. For example, here we see a flag saying whether the parser allows there to be multiple JSON values at the top level of a document (a very common variant of the language). As a best practice, these should always be provided with a default value (if possible) so that your users can create a `Reader` object as simply as possible.

## Writer

Whether your language implementation exposes a `Writer` depends entirely on the language. In the case of data formats like JSON and YAML, it makes a lot of sense and as such we have included them in our implementations. Naming them like `json::writer()` or `yaml::event_writer()`, that is with `writer` in the name and a prefix indicating if it is not what the "default" writer would be (for example, `yaml::event_writer()` for YAML event files) is considered best practice.

# Rewriter

One of the unique affordances given by Trieste as a library is that the workflow to translate from one language to another is the same as everything else: multi-pass AST rewriting. To provide an example of this we expose `yaml::to_json()`, which returns a `Rewriter` that converts a YAML AST to a JSON AST. Best practice for exposing these helpers is as show here, that is to say `<namespace>::to_<other namespace>`.
