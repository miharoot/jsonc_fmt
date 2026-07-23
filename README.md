# jsonc_fmt

**[Русский](README.ru.md) | English**

A small, portable, single-file JSON formatter and validator written in plain C (C99), with no external dependencies. Builds with a single `cc` command on any Linux system.

- Pretty-prints JSON with configurable indentation.
- Understands `//` and `/* */` comments (a non-standard but widely used JSON extension, often called JSONC) and **preserves them** when formatting.
- Validates JSON and, on a syntax error, reports the exact **line and column** where it was found.
- Writes to stdout by default; also supports writing to a file or formatting a file in place.

## Contents

- [Build](#build)
- [Quick start](#quick-start)
- [Options](#options)
- [How comments are handled](#how-comments-are-handled)
- [Compact arrays (`--compress`)](#compact-arrays---compress)
- [Blank line between elements (`--fine`)](#blank-line-between-elements---fine)
- [Validation and errors](#validation-and-errors)
- [Exit codes](#exit-codes)
- [Portability across Linux distributions](#portability-across-linux-distributions)

## Build

Only a C compiler (gcc/clang) is required — no libraries beyond the standard libc.

```sh
cc -std=c99 -O2 -Wall -Wextra -o jsonc_fmt jsonc_fmt.c
```
Static:
```sh
cc -std=c99 -O2 -Wall -Wextra -static -o jsonc_fmt jsonc_fmt.c
```



## Quick start

```sh
# from a file to stdout
jsonc_fmt config.json

# from stdin to stdout
cat config.json | jsonc_fmt

# no file and no pipe -> prints usage
jsonc_fmt

# write the result to another file
jsonc_fmt config.json -o config.formatted.json

# format a file in place
jsonc_fmt -w config.json
```

## Options

```
Usage: jsonc_fmt [options] [file]

  -i, --indent N          indent width in spaces (default: 2)
  -t, --tabs               use tabs for indentation
  -a, --align-comments     indent standalone comments to the current
                            nesting level instead of flushing them to
                            column 0
  -f, --fine                pretty JSON: insert a blank line between
                            adjacent object/array elements
  -c, --compress             never expand arrays of scalars onto multiple
                            lines, no matter how many elements they have
  -o, --output FILE        write the result to a file instead of stdout
  -s, --stdout              explicitly write to stdout (takes priority
                            over -o and -w if given together)
  -w, --in-place            format the input file in place (overwrite it);
                            requires an actual file, not stdin
      --no-comments        disallow // and /* */ (strict JSON)
  -h, --help                show help
```

If no file is given, or `-` is given, input is read from stdin. If run with no arguments and no redirected stdin (i.e. directly in a terminal), usage is printed instead of waiting for input.

## How comments are handled

`jsonc_fmt` understands `//` and `/* */` comments inside JSON (JSONC) and preserves them when formatting, following this rule:

- A comment that was the **only content on its line** in the source (nothing but whitespace before or after it on that line) is considered **"standalone"** and is flushed to column 0 on its own line in the output.
- A comment sitting on a line **next to code** (e.g. after a value, before a comma) is considered **"inline"** and stays attached to that same line of code, as it was.

```jsonc
{
// standalone comment — flushed to column 0
  "name": "Alice", // inline comment — stays on the code line
  "age": 30
}
```

The `-a, --align-comments` option changes this behavior for standalone comments: instead of flushing them to column 0, they are indented to match the current nesting level.

The `--no-comments` option disallows `//` and `/* */` entirely — if present in the input, this is treated as a syntax error (strict JSON).

## Compact arrays (`--compress`)

An array containing a **single** scalar value (string/number/`true`/`false`/`null`) is **never** expanded onto multiple lines — this behavior is always on:

```json
{
  "domain": ["geoip:ru"]
}
```

With `-c, --compress`, **no** "flat" array of scalars is expanded, regardless of how many elements it has:

```sh
$ echo '{"domain":["geoip:ru","geoip:cn","geoip:private"]}' | jsonc_fmt -c
{
  "domain": ["geoip:ru", "geoip:cn", "geoip:private"]
}
```

Without `-c`, such an array is expanded one element per line, as usual. This rule only applies to arrays with no nested objects/arrays and no comments inside — if either is present, the array is always expanded so nothing is lost.

## Blank line between elements (`--fine`)

The `-f, --fine` option inserts a blank line between adjacent container elements (objects or arrays) — that is, whenever a comma is immediately followed by a new `{` or `[`, and was itself immediately preceded by a `}` or `]`:

```sh
$ jsonc_fmt -f array.json
[
  {
    "id": 1,
    "name": "a"
  },

  {
    "id": 2,
    "name": "b"
  }
]
```

## Validation and errors

Before formatting, `jsonc_fmt` fully validates the JSON. If an error is found, the program **prints nothing to stdout** and instead prints a message to stderr with the exact line and column:

```sh
$ echo '{"a": 1, "b": [1,2,] }' | jsonc_fmt
Ошибка: неожиданный токен, ожидалось значение (строка 1, столбец 20)

$ printf '{\n  "a": 1,\n  "b": tru\n}\n' | jsonc_fmt
Ошибка: нераспознанный токен возле 't' (строка 3, столбец 8)

$ printf '{\n  "a": "unterminated\n}\n' | jsonc_fmt
Ошибка: незакрытая строка (перевод строки внутри строки) (строка 2, столбец 21)
```

(Error messages are currently in Russian only.)

Detected issues include: unterminated strings/comments, invalid control characters and escape sequences, malformed numbers (`01`, `.5`, `1.`, etc.), missing `,`/`:`, trailing/dangling commas, trailing garbage after the root value, and unexpected end of file — each with a precise position.

## Exit codes

| Code | Meaning                                                    |
|------|-------------------------------------------------------------|
| 0    | Success                                                      |
| 1    | JSON syntax error (message with position printed to stderr) |
| 2    | Usage error (bad arguments, file couldn't be opened, etc.)  |

## Portability across Linux distributions

The program uses nothing exotic — just standard C99 plus POSIX `isatty`/`fileno`. To get a binary that doesn't depend on the target system's glibc version, build it statically:

```sh
cc -std=c99 -O2 -Wall -Wextra -static -o jsonc_fmt jsonc_fmt.c
```

Such a binary can be copied to any other Linux machine of the same architecture and run with no libraries to install.

## License

Use, modify, and distribute freely.
