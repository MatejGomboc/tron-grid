Perform a thorough C++ code quality and bug-hunting audit on specified files or all source files in `src/`.

## What to Check

### Memory & Resource Safety

- Uninitialised variables
- Use-after-free or dangling pointers
- Missing `nullptr` checks before dereference
- Resource leaks (Vulkan handles, file handles, memory allocations)
- Missing cleanup in error paths
- Double-free or double-destroy of Vulkan objects

### Vulkan-Specific

- Missing error checks on `vkCreate*` / `vkAllocate*` calls (every `VkResult` must be checked)
- Incorrect struct `sType` fields
- Missing `pNext = nullptr` in Vulkan structs
- Incorrect queue family indices
- Swapchain image count assumptions
- Missing synchronisation (fences, semaphores, pipeline barriers)
- Using features without checking `vkGetPhysicalDeviceFeatures`

### Logic Errors

- Off-by-one errors in loops and array indexing
- Integer overflow/underflow
- Incorrect operator precedence
- Unreachable code or dead branches
- Missing `break` in switch cases
- Implicit narrowing conversions

### C++20 Best Practices

- Raw `new`/`delete` instead of smart pointers or RAII wrappers
- C-style casts instead of `static_cast`/`reinterpret_cast`
- Missing `const` where appropriate
- Missing `[[nodiscard]]` on functions returning error codes
- Using `NULL` instead of `nullptr`

### Platform-Specific

- Windows/Linux code paths missing from `#ifdef` blocks
- Platform-specific types used without guards
- Hardcoded paths or assumptions about OS behaviour

## Output Format

For each issue found, report:

1. **File and line number**
2. **Severity** (critical / warning / suggestion)
3. **Description** of the issue
4. **Suggested fix**

If no files are specified via $ARGUMENTS, audit all `.cpp` and `.h` files in `src/`. Summarise with a count of issues by severity at the end.
