# Project Trieste

Project Trieste is an experimental term rewriting system for experimental programming language development.

This research project is at an early stage and is open sourced to facilitate academic collaborations. We are keen to engage in research collaborations on this project, please do reach out to discuss this.

The project is not ready to be used outside of research.

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

Here's an example of how to build the `verona` sample and run the self-tests. Other build systems and compilers may work as well.

```sh
git clone https://github.com/microsoft/trieste
cd trieste
mkdir build
cd build
cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-14
ninja install
./dist/verona/verona test
```

## Using Trieste in Your Project

You can use Triest via FetchContent by including the following lines
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

## [Contributing](CONTRIBUTING.md)
