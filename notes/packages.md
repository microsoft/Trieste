# Packages

The string that represents a package is a resolver.

Packages go in `Top`, inside a class, where the class name is derived from the resolver. Two resolvers that end up at the same package should get the same class name.

On disk, packages are directories. The package name is the directory name. Package directories go in your build directory, not in the source directory.

## Build Directory

```ts
build
- packages
  - package1
  - ...
- debug
  - program1
  - program2
  - ...
- release
  - program1
  - program2
  - ...
```

## Resolvers

https://cmake.org/cmake/help/latest/module/ExternalProject.html#id1
- URL
- git
- local directory?

## Prologue

This could be added as the prologue to every program:

```ts
type std = "std lib resolver"
use std::builtin

(TypeAlias
  (Ident std)
  (TypeParams)
  (Type (TypeVar $0))
  (Type
    (Package (String "std lib resolver"))))
```
