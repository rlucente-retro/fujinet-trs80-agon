# Workspace Rules & Guidelines

## 1. Documentation & Formatting Standards: Strict No-LaTeX Rule
* **Standard Markdown Only:** All documentation, guides, specifications, commit messages, and assistant responses MUST use ONLY standard GitHub-Flavored Markdown.
* **NEVER Use LaTeX Formulas or MathJax Delimiters:**
  - DO NOT use display math blocks (`$$ ... $$`).
  - DO NOT use inline math delimiters (`$ ... $`).
  - DO NOT use LaTeX commands (e.g. `\text{...}`, `\frac{...}`, `\times`, `\approx`, `\sum`, `\pmod`, `\rightarrow`, `\lfloor`, `\rfloor`, etc.).
* **Formatting Mathematical & Technical Expressions:**
  - Express all arithmetic, divisors, baud calculations, LBA mappings, and equations using standard Markdown inline backticks:
    - Example: `Divisor = 18,432,000 / (16 * 115,200) = 10 (0x000A)`
    - Example: `Block LBA = BaseLBA + (LSN / 2)`
    - Example: `Byte Offset = Header Offset + (LSN * 256)`
  - Use standard ASCII or Unicode operators: `*` for multiplication, `/` for division, `->` or `→` for arrows, `≈` for approximations, `+/-` or `±` for tolerances.
  - Always enclose assembly labels, registers, and symbols ending in `$` (such as `DCT0$`, `KFLAG$`, `LDRV$`, `BAR$`) in inline backticks (e.g. `` `DCT0$` ``) or fenced code blocks so GitHub's Markdown parser never interprets trailing dollar signs as math delimiters.
  - Format hexadecimal addresses using standard `0x` prefixes (e.g. `0xD0`, `0x00F400`) rather than standalone `$` prefixes (e.g. avoid unquoted `$D0`).
