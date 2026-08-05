# Golden test suites

Trieste's CMake test harness runs commands, captures their exit code and
standard streams, and compares selected outputs with files stored in the
source tree. It also supports named command graphs, so one node can consume a
transient artifact produced by another.

## Concepts and control flow

A **suite** is one test directory registered with `testsuite()`. A suite can
contain multiple **collections**, each with its own `.cmake` file describing
which tests belong to that collection and how they are run. A collection maps
each selected candidate input file to one or more **nodes**:

- A node has a suite-local name and runs one command.
- `GOLDENS` are command outputs stored in the source tree and compared by
  CTest.
- `ARTIFACTS` are required transient outputs kept only in the build tree.
- `DEPENDS` orders nodes and requires each prerequisite to verify successfully.

Configuration proceeds in this order:

1. `testsuite()` discovers collections and candidate files.
2. `TESTSUITE_REGEX` selects candidates.
3. `TESTSUITE_DEFINE` invokes a callback which registers named nodes.
4. The completed node graph is validated and emitted as CTest tests and update
   targets.

## Registering a suite

Create a `CMakeLists.txt` in the test directory:

```cmake
include("${trieste_SOURCE_DIR}/cmake/testsuite.cmake")
testsuite(my-language)
```

## Collections

Each suite can have multiple collections of tests. Every `.cmake` file next to
the suite's `CMakeLists.txt` is a collection file:

```text
testsuite/
|-- CMakeLists.txt
|-- compiler.cmake
|-- linter.cmake
`-- examples/
```

The collection file configures how that collection is discovered and run. It
selects candidate files using `TESTSUITE_REGEX`.
`TESTSUITE_DEFINE` names the callback which creates nodes for each selected
file.

`testsuite()` loads every adjacent collection file. All collections select
from the same recursive list of files beneath the suite directory, but each
collection applies its own selection rules and execution configuration.
Collections normally represent independent tools or test configurations. For
example, `compiler.cmake` may register both compile and execute nodes because
they form one dependency graph, while `linter.cmake` independently checks
source diagnostics.

## Defining a collection

The following collection compiles each `.source` file and runs the resulting
transient bytecode:

```cmake
set(TESTSUITE_REGEX "\\.source$")
set(TESTSUITE_DEFINE define_tests)

function(define_tests source)
  cmake_path(REMOVE_EXTENSION source LAST_ONLY OUTPUT_VARIABLE stem)
  set(compile "${stem}/compile")
  set(run "${stem}/run")

  testsuite_output_path(
    bytecode NODE "${compile}" FILE "${stem}.bc")

  testsuite_add_test(
    NAME "${compile}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    GOLDENS
      exit_code.txt
      stdout.txt
      stderr.txt
    ARTIFACTS "${stem}.bc"
    COMMAND
      "$<TARGET_FILE:compiler>" "${source}" -o "${bytecode}")

  testsuite_add_test(
    NAME "${run}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS "${compile}"
    GOLDENS
      exit_code.txt
      stdout.txt
      stderr.txt
    COMMAND
      "$<TARGET_FILE:interpreter>" "${bytecode}")
endfunction()
```

`TESTSUITE_DEFINE` names the callback invoked once for every selected
candidate. The callback registers nodes with `testsuite_add_test()`, but it may
return without registering a node when a candidate needs additional
path- or content-based filtering that `TESTSUITE_REGEX` cannot express.
`COMMAND` must be the final metadata keyword.

For example, a collection can select every `.source` file and keep only files
whose basename matches their enclosing directory:

```cmake
function(define_tests source)
  cmake_path(GET source PARENT_PATH directory)
  cmake_path(GET source STEM basename)
  cmake_path(GET directory FILENAME directory_name)
  if(NOT basename STREQUAL directory_name)
    return()
  endif()

  testsuite_add_test(...)
endfunction()
```

### Node properties

| Property | Meaning |
|---|---|
| `NAME` | Suite-local node identity and relative golden-directory path. |
| `COMMAND` | Executable and argument list. Must appear last. |
| `WORKING_DIRECTORY` | Directory in which the command runs. |
| `DEPENDS` | Suite-local node names which must verify first. |
| `GOLDENS` | Exact generated files to compare and copy. |
| `ARTIFACTS` | Required transient files which are never copied to the source tree. |
| `TIMEOUT` | Positive command timeout in seconds; defaults to 20. |
| `VALIDATOR` | Optional CMake script executed with `OUTPUT_DIR` set. |

If `GOLDENS` is omitted, it defaults to `exit_code.txt`, `stdout.txt`, and
`stderr.txt`. An explicit list must include `exit_code.txt`.

The harness normalizes relative `NAME`, `DEPENDS`, `GOLDENS`, and `ARTIFACTS`
paths. It rejects absolute paths, traversal outside the suite, duplicate
nodes, unknown dependencies, and dependency cycles. The harness requires an
out-of-source build.

`testsuite_output_path(out NODE name FILE path)` returns the deterministic
build-tree location of a node output. The referenced node may be declared
later, but it must exist when registration finishes. A collection must name
the producer in `DEPENDS` when another node consumes that path.

## Dependency semantics

Each node is one public `<suite>/<name>` CTest entry. The test runs the
command, captures its output, validates every declared file, and compares its
goldens. It sets up a verified fixture which dependent nodes require. If a
prerequisite command or comparison fails, dependents are reported as
`Not Run`; unrelated graph branches remain parallel.

Numeric nonzero exit codes are ordinary test output and may be expected by an
`exit_code.txt` golden. Launch failures, timeouts, and signals are harness
failures. Expected compile-error tests normally do not register a run node.

Selecting a leaf with `ctest -R` automatically includes its transitive
prerequisites:

```sh
ctest --test-dir build -R '^my-language/example/run$'
```

## Updating goldens

Each suite provides:

```sh
cmake --build build --target my-language-update-dump
```

The global `update-dump` target aggregates all suites.

The update graph has the same dependency edges as CTest. A node reruns its
command, validates all declared outputs, copies only `GOLDENS`, and then
unlocks dependents. Missing artifacts or invalid output stop dependent update
commands. `ARTIFACTS` and unlisted generated files stay in the build tree.

Every update starts from a fresh build-tree output directory and requires every
declared golden file to be generated, so a separate clean update is
unnecessary. Remove obsolete source-tree files explicitly when deleting them
from `GOLDENS`.

Both verification and update use the same generated CMake configuration file
for each node and the same executor. The file records the command, paths,
declared outputs, validator, and timeout. Validators run in a separate CMake
process after declared outputs have been checked, and outputs are checked
again afterward.

## Command-list limitations

Commands are passed without a shell. Empty and semicolon-bearing arguments are
unsupported because CMake cannot preserve them through its list model.
Generator-expression results are checked immediately before execution.

When `TRIESTE_GENERATE_LAUNCH_JSON` is enabled, entries from all suites are
written to one `.vscode/launch.json`. Nodes with `DEPENDS` also receive a
`preLaunchTask` in `.vscode/tasks.json`. Before the debugger starts, that task
verifies the node's direct prerequisites through CTest; fixture requirements
automatically include their transitive prerequisites. A failed prerequisite
prevents the debugger from starting.

With a multi-config generator, target paths and prerequisite tasks are
evaluated for the first configuration in `CMAKE_CONFIGURATION_TYPES`.
`launch.json` and `tasks.json` are generated files and replace existing files
at those paths. The prerequisite task runs tests but does not build tool
targets, so build the project before launching the debugger.

## Migrating a legacy collection

The legacy collection interface is no longer supported. Legacy collections
configured one command with `TESTSUITE_EXE`,
`toolinvoke()`, and `test_output_dir()`. Replace those hooks with one
`TESTSUITE_DEFINE` callback. The callback receives the same relative candidate
path and registers a node directly.

For example, this legacy collection:

```cmake
set(TESTSUITE_REGEX "\\.source$")
set(TESTSUITE_EXE "$<TARGET_FILE:compiler>")

macro(toolinvoke out test_file output_dir)
  set(${out} "${test_file}")
endmacro()

function(test_output_dir out test)
  cmake_path(REMOVE_EXTENSION test LAST_ONLY OUTPUT_VARIABLE stem)
  set(${out} "${stem}_out" PARENT_SCOPE)
endfunction()
```

becomes:

```cmake
set(TESTSUITE_REGEX "\\.source$")
set(TESTSUITE_DEFINE define_test)

function(define_test test)
  cmake_path(REMOVE_EXTENSION test LAST_ONLY OUTPUT_VARIABLE stem)
  cmake_path(GET test PARENT_PATH test_dir)
  cmake_path(GET test FILENAME test_file)

  testsuite_add_test(
    NAME "${stem}_out"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}"
    COMMAND "$<TARGET_FILE:compiler>" "${test_file}")
endfunction()
```

Using the previous output directory as `NAME` preserves the existing golden
directory. The default `GOLDENS` already covers `exit_code.txt`, `stdout.txt`,
and `stderr.txt`.

Apply the following mappings when the legacy collection did more:

| Legacy behavior | Replacement |
|---|---|
| Additional committed output files | List them explicitly in `GOLDENS`. |
| Generated files not committed as goldens | List them in `ARTIFACTS`. |
| `TESTSUITE_VALIDATOR` | Set `VALIDATOR` on each applicable node. |
| Longer command timeout | Set `TIMEOUT` on the node. |
| Compile followed by execute | Register two nodes and add `DEPENDS` to the execute node. |
| Execute consumes compiler output | Obtain its path with `testsuite_output_path()`. |

The migration changes CTest presentation: each node is one
`<suite>/<name>` test instead of separate command and per-file comparison
tests. Named nodes require an out-of-source build. After converting, run
`update-dump` and inspect the source tree; only declared `GOLDENS` should
change, while `ARTIFACTS` must remain in the build tree.
