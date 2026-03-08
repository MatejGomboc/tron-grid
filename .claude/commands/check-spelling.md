Audit all changed or specified files for British English spelling enforcement.

## Rules

All documentation, comments, and user-facing strings MUST use British English spelling.

## Common Violations to Check

| American (wrong) | British (correct) |
|-------------------|-------------------|
| color | colour |
| behavior | behaviour |
| optimize | optimise |
| center | centre |
| license (noun) | licence |
| meter | metre |
| synchronize | synchronise |
| initialize | initialise |
| analyze | analyse |
| organize | organise |
| recognize | recognise |
| customize | customise |
| utilize | utilise |
| minimize | minimise |
| maximize | maximise |
| normalize | normalise |
| authorize | authorise |
| serialize | serialise |
| finalize | finalise |
| paralyze | paralyse |
| catalog | catalogue |
| dialog | dialogue |
| gray | grey |
| defense | defence |
| offense | offence |
| pretense | pretence |
| fulfill | fulfil |
| enrollment | enrolment |
| modeling | modelling |
| traveling | travelling |
| canceled | cancelled |
| labeled | labelled |

## Exceptions

- **Code identifiers** may use American spelling where it matches library/API conventions (e.g., Vulkan API names like `VkColorSpaceKHR`)
- **`CODE_OF_CONDUCT.md`** — off limits, do not modify
- **`LICENCE`** — off limits, legal document
- **External library names** (e.g., `meshoptimizer`) are proper nouns
- **Quoted text** from external sources

## What to Check

If no files are specified via $ARGUMENTS, check all `.md`, `.cpp`, `.h`, and `.yml` files in the repo. Report every violation with file path, line number, the offending word, and its British equivalent. Then offer to fix them all.
