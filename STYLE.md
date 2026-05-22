# qublk coding style

The authoritative spec is `.clang-format` in the repository root. Run
`make format` before committing to apply it, and `make format-check` to verify
without modifying files.

Highlights, all enforced by clang-format:

- **Indentation**: tabs, 8 columns wide. Continuation lines indented by one tab.
- **Line width**: 99 columns hard limit.
- **Braces** (`BreakBeforeBraces: WebKit`):
  - Function definitions: opening brace on its own line.
  - Control flow (`if`, `else`, `for`, `while`, `do`, `switch`): opening brace on
    the same line as the keyword.
  - Always brace single-statement bodies of `if/else/for/while`.
- **Return type on its own line** for every function definition and declaration —
  including `static` and including in header files.
- **Pointer alignment** is to the identifier: `struct foo *bar`.
- **`#include` order is preserved** (`SortIncludes: Never`). Group system headers,
  then third-party, then local.

Rules clang-format cannot check, but we follow:

- **Variable declarations go at the top of each function** (C89-style). No
  mid-block declarations. Declarations in the init clause of a `for` loop are an
  allowed exception when the variable is genuinely loop-scoped.
- **Collapse same-type declarations** onto one line: prefer `int rc, res;` over
  two separate lines. Different types still go on separate lines. Initializers
  are fine: `const char *uri = NULL, *be = NULL;`.
