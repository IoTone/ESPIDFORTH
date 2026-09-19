# Examples

Runnable Forth for the ESPIDFORTH REPL. Each file is plain text — paste a
line at a time at the `ok>` prompt, or send the whole file with your
terminal's "send file" command.

| File | Covers |
|------|--------|
| `01-stack.fs`     | The data stack, postfix arithmetic, `.s` |
| `02-defining.fs`  | `:` … `;`, factoring words, `."` |
| `03-control.fs`   | `if/else/then`, `do/loop`, `+loop`, `i`/`j`, `begin/until` |
| `04-memory.fs`    | `variable`, `constant`, `!`/`@`, `here`/`allot` |
| `05-chip.fs`      | The built-in ESP-IDF FFI words |
| `06-bases.fs`     | `hex`/`decimal`, literal prefixes, `s"`/`type`, `pick` |

## Board projects

| Directory | Board | What it shows |
|-----------|-------|---------------|
| `xiao-trailcam/` | XIAO ESP32S3 **Sense** | Camera + microSD. An 11-word C vocabulary, with capture policy written in Forth and redefinable while the camera runs. |


Every file runs without error against the v0.5.0 engine and leaves the
stack empty. Comments use `\`, which the interpreter ignores to end of line.

Tutorials that walk through these — plus four more that add your own words
in C — are at <https://iotone.github.io/ESPIDFORTH/tutorials.html>.
