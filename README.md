# Project Trieste

Project Trieste is an experimental term rewriting system for experimental programming language development.

This research project is at an early stage and is open sourced to facilitate academic collaborations. We are keen to engage in research collaborations on this project, please do reach out to discuss this.

The project is not ready to be used outside of research.

## Using Trieste

Trieste is a header-only C++20 library. To get started, you'll need to define your own `trieste::Driver`, and run it from `main`:

```c++
#include <driver.h>

int main(int argc, char** argv)
{
  // Define your driver...
  trieste::Driver driver(...);
  return driver.run(argc, argv);
}
```

## Building the Samples

Here's an example of how to build the `verona` sample and run the self-tests:

```sh
git clone https://github.com/microsoft/trieste
cd trieste
mkdir build
cd build
cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-12
ninja install
./dist/verona/verona test
```

## [Contributing](CONTRIBUTING.md)
