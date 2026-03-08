# Style Guide

Code style conventions for TronGrid.

---

## General Rules

| Rule | Setting |
|------|---------|
| Indentation | 4 spaces (no tabs) |
| Max line length | 170 characters |
| Charset | UTF-8 |
| Final newline | Always |
| Trailing whitespace | Trim (except Markdown) |

These rules are enforced by `.editorconfig`. Install the EditorConfig plugin for your editor:

- **VS Code:** [EditorConfig for VS Code](https://marketplace.visualstudio.com/items?itemName=EditorConfig.EditorConfig)

---

## Single Source of Truth

Avoid duplicating information across files. Each piece of information should have one canonical location.

| Information | Canonical Source |
|-------------|-----------------|
| Build commands | `README.md` § Building |
| Formatting rules | `.clang-format`, `.editorconfig` |
| Security policy | `SECURITY.md` |
| Roadmap & phases | `docs/VISION.md` § Phased Roadmap |

**Guidelines:**

- Reference the canonical source instead of duplicating content
- If information must appear in multiple places (e.g., PR template checklists), keep it minimal
- When updating information, update the canonical source first
- Cross-reference using `filename` § Section Name format

---

## C++

### Formatting

Use `.clang-format` (LLVM-based). CI does not currently enforce this, but all code should be formatted before committing.

Key settings:

| Setting | Value |
|---------|-------|
| Brace style | Allman for functions/namespaces, attached for classes/structs/enums |
| Indent | 4 spaces |
| Column limit | 170 |
| Pointer alignment | Left (`int* ptr`) |

### Naming Conventions

| Item | Convention | Example |
|------|------------|---------|
| Namespaces | snake_case | `tron_grid` |
| Types / Classes | PascalCase | `SwapchainImage` |
| Functions | snake_case | `create_device` |
| Constants | SCREAMING_SNAKE_CASE | `MAX_FRAMES_IN_FLIGHT` |
| Variables | snake_case | `frame_index` |
| Member variables | snake_case | `device_handle` |
| Macros | SCREAMING_SNAKE_CASE | `VK_NO_PROTOTYPES` |

### Language Standard

C++20. No exceptions (the language feature — the word "exceptions" here refers to C++ exception handling, which we avoid).

---

## YAML (GitHub Actions)

### Indentation

**4 spaces** for structure levels — aligned with project-wide convention.

```yaml
jobs:
    build:
        name: Build
        runs-on: ubuntu-latest

        steps:
            - name: Checkout
              uses: actions/checkout@v6

            - name: Build
              run: cmake --workflow --preset=linux-x11-gcc
```

### List Item Indentation

List items use **2-space continuation** from the `-` character (standard YAML behaviour):

```yaml
updates:
    - package-ecosystem: "github-actions"
      directory: "/"
      schedule:
        interval: "weekly"
```

### Multi-line Scripts (`run: |`)

Shell script content inside `run: |` blocks uses **4-space indentation** for shell constructs (if/else, loops):

```yaml
            - name: Example step
              shell: bash
              run: |
                if [[ -n "$VAR" ]]; then
                    echo "Variable is set"
                else
                    echo "Variable is not set"
                fi
```

### Structure

- Blank line between top-level keys (`on`, `env`, `jobs`)
- Blank line between jobs
- Blank line before `steps:` in complex jobs
- Comments on their own line, not inline

---

## JSON

### Indentation

**4 spaces**.

```json
{
    "key": "value",
    "nested": {
        "item": 123
    }
}
```

---

## Markdown

### Headings

Use ATX-style headings with blank lines before and after:

```markdown
## Section Title

Content here.
```

### Lists

Use `-` for unordered lists, `1.` for ordered lists.

### Code Blocks

Always specify the language:

````markdown
```cpp
int main()
{
    return 0;
}
```
````

### Trailing Whitespace

Markdown files are exempt from trailing whitespace trimming (needed for line breaks).

### Linting

Use `markdownlint-cli2` to lint Markdown files. Configuration is in `.markdownlint.json` and `.markdownlint-cli2.jsonc`.

```bash
npx markdownlint-cli2 "**/*.md"
```

---

## British Spelling

Use British spelling in all documentation, comments, and user-facing strings:

| American | British |
|----------|---------|
| color | colour |
| optimize | optimise |
| behavior | behaviour |
| center | centre |
| license | licence |
| meter | metre |
| synchronize | synchronise |
| initialize | initialise |

Code identifiers may use American spelling where it matches library/API conventions (e.g., Vulkan API names).

---

*Last updated: 2026-03-08*
