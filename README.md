# Project Trieste

Project Trieste is a term rewriting system designed for rapidly prototyping programming languages.
Trieste provides three C++ DSLs to enable the rapid prototyping:

* Parsing - Enables generation of an untyped abstract syntax tree (AST) from one or many input files.
* Rewriting - Enables the restructuring and analysis of the AST. It can be used to simplify, elaborate to alternative representations, and lower to backend IRs or bytecodes.
* Well-formedness - Trieste provides a DSL for checking that the current AST conforms to a specification. 

Using the well-formedness definitions Trieste can rapidly harden a language by automatically checking conformance to the specification.
Additionally, Trieste uses the well-formedness definitions to generate test cases to fuzz each rewriting pass conforms with its specification.

## Getting Started

If you want to dive right into understanding how to use Trieste, take
a look at the [`infix` tutorial language](./samples/infix/README.md),
which will walk you through implementing a simple calculator language
in Trieste.

## Using Trieste

Trieste is a header-only C++20 library. To get started, you'll need to define your own `trieste::Driver`, and run it from `main`:

```c++
#include <trieste/driver.h>

int main(int argc, char** argv)
{
  // Define your driver...
  trieste::Driver driver(...);
  return driver.run(argc, argv);
}
```

## Building the Samples

Here's an example of how to build the `infix` sample and run the self-tests. Other build systems and compilers may work as well.

```sh
git clone https://github.com/microsoft/trieste
cd trieste
mkdir build
cd build
cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-14
ninja install
./dist/infix/infix test
```

## Regex Benchmark (Opt-In)

Trieste includes an opt-in benchmark that compares the internal regex engine
against RE2.

- Option: `TRIESTE_BUILD_REGEX_BENCHMARK`
- Default: `OFF`
- RE2 is fetched and built only when this option is enabled.
- The benchmark target is not added to `ctest`.

Default behavior (no benchmark, no RE2 fetch/build):

```sh
cmake -S . -B build-default
cmake --build build-default
```

Benchmark-enabled behavior:

```sh
cmake -S . -B build-bench \
  -DTRIESTE_ENABLE_TESTING=ON \
  -DTRIESTE_BUILD_REGEX_BENCHMARK=ON
cmake --build build-bench --target trieste_regex_engine_benchmark
./build-bench/test/trieste_regex_engine_benchmark
```

The benchmark output includes deterministic workload metadata and summary ratios
for compile+match and match-only timings.

## Regex Syntax Modes

Regex syntax policy for strict iregexp compatibility versus extended Trieste
behavior is defined in [`notes/regex-syntax-policy.md`](notes/regex-syntax-policy.md).

## Using Trieste in Your Project

You can use Trieste via FetchContent by including the following lines
in your CMake:

``` cmake
FetchContent_Declare(
  trieste
  GIT_REPOSITORY https://github.com/microsoft/Trieste
  GIT_TAG        a2a7fada4ab5250a4f8d1313b749ad336202841b
)

FetchContent_MakeAvailable(trieste)
```

And then adding it as a target link library, e.g.

``` cmake
target_link_libraries(verona
  Threads::Threads
  CLI11::CLI11
  trieste::trieste
  )
```

## Contributing

If you are interested in contributing to Trieste, please see our [contributing document](CONTRIBUTING.md).
