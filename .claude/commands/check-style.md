Review all changed or specified files for STYLE.md compliance.

Check against these rules (from `STYLE.md` and `.clang-format`):

## C++ Formatting
- Allman braces for functions and namespaces, attached for classes/structs/enums
- 4-space indentation (no tabs)
- 170-column limit
- Left-aligned pointers (`int* ptr`, not `int *ptr`)
- No short forms on single lines (no single-line if/for/while/functions)

## C++ Naming
- Namespaces: `snake_case`
- Types/Classes: `PascalCase`
- Functions: `snake_case`
- Constants: `SCREAMING_SNAKE_CASE`
- Variables/Members: `snake_case`
- Macros: `SCREAMING_SNAKE_CASE`

## YAML (GitHub Actions)
- 4-space structure indentation
- 2-space continuation from list items
- Blank lines between top-level keys and between jobs
- No inline comments

## JSON
- 4-space indentation

## Markdown
- ATX-style headings
- Dash lists (`-`), numeric ordered lists (`1.`)
- Fenced code blocks with language specified

Report every violation with file path, line number, and what's wrong. If no files are specified via $ARGUMENTS, check all files modified since the last commit.
