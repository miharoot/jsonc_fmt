# jsonc_fmt

[English](#english) | [Русский](#русский)

---

## English

A small, portable, single-file JSON formatter and validator written in plain C (C99), no external dependencies. Builds with one `cc` command on any Linux system.

- Pretty-prints JSON with configurable indentation.
- Understands `//` and `/* */` comments (a common, non-standard JSON extension known as JSONC) and **preserves them** when formatting.
- Keeps small scalar arrays and small flat objects on a single line, and can be told to compress more aggressively.
- Validates JSON and, on a syntax error, reports the exact **line and column** where it was found.
- Writes to stdout by default; also supports writing to a file or formatting a file in place.

### Contents

- [Build](#build)
- [Quick start](#quick-start)
- [Options](#options)
- [How comments are handled](#how-comments-are-handled)
- [Compact arrays and objects (`--compress`)](#compact-arrays-and-objects---compress)
- [Blank line between elements (`--fine`)](#blank-line-between-elements---fine)
- [Validation and errors](#validation-and-errors)
- [Exit codes](#exit-codes)
- [Portability across Linux systems](#portability-across-linux-systems)

### Build

Only a C compiler is required (gcc/clang) — no libraries beyond the standard libc.

```sh
cc -std=c99 -O2 -Wall -Wextra -o jsonc_fmt jsonc_fmt.c
```

For a binary that doesn't depend on the target machine's glibc version (see [Portability](#portability-across-linux-systems)):

```sh
cc -std=c99 -O2 -Wall -Wextra -static -o jsonc_fmt jsonc_fmt.c
```

### Quick start

```sh
# from a file to stdout
jsonc_fmt config.json

# from stdin to stdout
cat config.json | jsonc_fmt

# no file and no pipe -> usage is printed
jsonc_fmt

# write the result to another file
jsonc_fmt config.json -o config.formatted.json

# format a file in place
jsonc_fmt -w config.json
```

### Options

```
Usage: jsonc_fmt [options] [file]

  -i, --indent N          indent width in spaces (default 2)
  -t, --tabs               use tab characters for indentation
  -a, --align-comments     indent even those standalone comments that
                            started exactly at the first character of
                            their source line (by default those are
                            flushed to column 0 — see below)
  -f, --fine                pretty JSON: blank line between adjacent
                            object/array elements
  -c, --compress            don't expand small scalar arrays/objects,
                            see below
  -o, --output FILE        write the result to a file instead of stdout
  -s, --stdout              write to stdout explicitly (takes priority
                            over -o and -w if given together)
  -w, --in-place            format the input file in place (overwrite
                            it); requires an actual file, not stdin
      --no-comments        disallow // and /* */ (strict JSON)
  -h, --help                show help
```

If no file is given, or `-` is given, input is read from stdin. If run with no arguments and no redirected stdin (i.e. directly in a terminal), usage is printed instead of waiting for input.

### How comments are handled

`jsonc_fmt` understands `//` and `/* */` comments inside JSON (JSONC) and preserves them when formatting, following this rule:

- A comment that has nothing but whitespace before it on its source line (i.e. it's alone on its line) is placed on its **own line** in the output.
  - If it started at **exactly the first character of that line** (column 0/1, not even a single space of indentation) — it is flushed to **column 0** in the output.
  - Otherwise (alone on its line, but indented in the source) — it stays on its own line but is **indented normally**, to the current nesting level — the same as `--align-comments` would do.
- A comment sitting on a line **next to code** (e.g. after a value, before a comma) stays attached to that same line of code, as it was.

```jsonc
{
// this one started at column 0 in the source -> flushed to column 0
    // this one was indented in the source -> stays on its own line,
    // but gets normal indentation instead
  "name": "Alice", // this one follows code -> stays attached to the line
  "age": 30
}
```

`-a, --align-comments` overrides the column-0 special case: with this flag, *every* standalone comment gets normal indentation, even ones that started at column 0/1 in the source.

`--no-comments` disallows `//` and `/* */` entirely — if either appears in the input, it is treated as a syntax error (strict JSON).

### Compact arrays and objects (`--compress`)

An array consisting of a **single** scalar value (string/number/`true`/`false`/`null`) is **never expanded** to multiple lines — this behavior is always on, with no length limit:

```json
{
  "domain": ["geoip:ru"]
}
```

With `-c, --compress`, two more kinds of structures are kept on one line, **provided the resulting line does not exceed 150 characters** (measured from the start of that line):

**1. Scalar arrays with up to 5 elements:**

```sh
$ echo '{"domain":["domain:ru","domain:su","geosite:vk","geosite:YANDEX"]}' | jsonc_fmt -c
{
  "domain": ["domain:ru", "domain:su", "geosite:vk", "geosite:YANDEX"]
}
```

**2. Objects with up to 5 fields, all of them scalar values:**

```sh
$ jsonc_fmt -c noise.json
{
  "noise": [
    { "rand": "10-30", "randRange": "0-255", "delay": "5-15" },
    { "rand": "20-60", "randRange": "0-255", "delay": "10-25" },
    { "rand": "5-15", "randRange": "0-255", "delay": "1-8" }
  ]
}
```

Here the outer array stays expanded (its elements are objects, not scalars), while each individual object — having 5 or fewer scalar fields and fitting within 150 characters — stays on one line.

Without `-c`, both kinds of structures are always expanded, one item per line. With `-c`, an array/object that has *more* than 5 items, or whose inline form would exceed 150 characters, is still expanded normally. The rule only applies to "flat" arrays/objects — no nested objects/arrays and no comments inside; if either is present, the structure is always expanded so nothing is lost.

### Blank line between elements (`--fine`)

The `-f, --fine` option inserts a blank line between adjacent container elements (objects or arrays) — i.e. whenever a `{` or `[` immediately follows a comma that itself follows a `}` or `]`:

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

### Validation and errors

Before formatting, `jsonc_fmt` fully validates the JSON. If an error is found, the program **prints nothing to stdout** and instead prints a message to stderr with the exact line and column:

```sh
$ echo '{"a": 1, "b": [1,2,] }' | jsonc_fmt
Ошибка: неожиданный токен, ожидалось значение (строка 1, столбец 20)

$ printf '{\n  "a": 1,\n  "b": tru\n}\n' | jsonc_fmt
Ошибка: нераспознанный токен возле 't' (строка 3, столбец 8)

$ printf '{\n  "a": "unterminated\n}\n' | jsonc_fmt
Ошибка: незакрытая строка (перевод строки внутри строки) (строка 2, столбец 21)
```

(Error messages are currently in Russian.) Detected issues include: unterminated strings/comments, invalid control characters and escape sequences, malformed numbers (`01`, `.5`, `1.`, etc.), missing `,`/`:`, extra/trailing commas, trailing garbage after the root value, and unexpected end of file — each with an exact position.

### Exit codes

| Code | Meaning                                              |
|------|--------------------------------------------------------|
| 0    | Success                                                 |
| 1    | JSON syntax error (message with position on stderr)     |
| 2    | Usage error (bad arguments, file couldn't be opened, etc.) |

### Portability across Linux systems

The program uses nothing exotic — just standard C99 plus POSIX `isatty`/`fileno`. To get a binary that doesn't depend on the target system's glibc version, build it statically:

```sh
cc -std=c99 -O2 -Wall -Wextra -static -o jsonc_fmt jsonc_fmt.c
```

Such a binary can be copied to any other Linux machine of the same architecture and run without installing any libraries.

### License

Use, modify, and distribute freely under GPL-3.0 license.

---

## Русский

Небольшой переносимый однофайловый JSON-форматтер и валидатор на чистом C (C99), без внешних зависимостей. Собирается одной командой `cc` на любом Linux.

- Форматирует (pretty-print) JSON с настраиваемыми отступами.
- Понимает `//` и `/* */` комментарии (нестандартное, но широко используемое расширение JSON — JSONC) и **не выбрасывает их** при форматировании.
- Оставляет небольшие массивы скаляров и небольшие плоские объекты на одной строке, а с опцией — сжимает ещё агрессивнее.
- Валидирует JSON и при ошибке синтаксиса точно указывает **строку и столбец**, где она обнаружена.
- По умолчанию результат печатается в stdout; есть режимы записи в файл и форматирования "на месте".

### Содержание

- [Сборка](#сборка)
- [Быстрый старт](#быстрый-старт)
- [Опции](#опции)
- [Как обрабатываются комментарии](#как-обрабатываются-комментарии)
- [Компактные массивы и объекты (`--compress`)](#компактные-массивы-и-объекты---compress)
- [Пустая строка между элементами (`--fine`)](#пустая-строка-между-элементами---fine)
- [Валидация и ошибки](#валидация-и-ошибки)
- [Коды возврата](#коды-возврата)
- [Переносимость между разными Linux](#переносимость-между-разными-linux)

### Сборка

Нужен только компилятор C (gcc/clang) — никаких библиотек, кроме стандартной libc.

```sh
cc -std=c99 -O2 -Wall -Wextra -o jsonc_fmt jsonc_fmt.c
```

Чтобы получить бинарник, не зависящий от версии glibc целевой машины (см. [Переносимость](#переносимость-между-разными-linux)):

```sh
cc -std=c99 -O2 -Wall -Wextra -static -o jsonc_fmt jsonc_fmt.c
```

### Быстрый старт

```sh
# из файла в stdout
jsonc_fmt config.json

# из stdin в stdout
cat config.json | jsonc_fmt

# без файла и без пайпа — печатается справка
jsonc_fmt

# записать результат в другой файл
jsonc_fmt config.json -o config.formatted.json

# отформатировать файл "на месте"
jsonc_fmt -w config.json
```

### Опции

```
Использование: jsonc_fmt [опции] [файл]

  -i, --indent N          ширина отступа в пробелах (по умолчанию 2)
  -t, --tabs               использовать табуляцию для отступов
  -a, --align-comments     отступать даже те одинокие комментарии,
                            что в исходнике начинались ровно с первого
                            символа строки (по умолчанию такие
                            прижимаются к колонке 0 — см. ниже)
  -f, --fine                красивый JSON: пустая строка между соседними
                            элементами-объектами/массивами
  -c, --compress            не разворачивать небольшие массивы/объекты
                            из скаляров, см. ниже
  -o, --output FILE        писать результат в файл вместо stdout
  -s, --stdout              писать результат в stdout явно (приоритет
                            над -o и -w, если указаны вместе)
  -w, --in-place            форматировать входной файл "на месте"
                            (перезаписать его же); требует файл, не stdin
      --no-comments        запретить // и /* */ (строгий JSON)
  -h, --help                показать справку
```

Если файл не указан или указан `-`, читается stdin. Без аргументов и без перенаправленного stdin (то есть при запуске напрямую в терминале) печатается справка.

### Как обрабатываются комментарии

`jsonc_fmt` понимает `//` и `/* */` комментарии внутри JSON (JSONC) и сохраняет их при форматировании по следующему правилу:

- Комментарий, перед которым на его строке в исходнике нет ничего, кроме пробелов (то есть он один на строке), выводится на **отдельной строке**.
  - Если он начинался **ровно с первого символа этой строки** (столбец 0/1, без единого отступа) — он прижимается к **колонке 0** в результате.
  - Иначе (один на строке, но с отступом в исходнике) — он остаётся на своей отдельной строке, но **форматируется как обычно**: с отступом по текущему уровню вложенности — так же, как это делает `--align-comments`.
- Комментарий, стоящий на строке **рядом с кодом** (например, после значения перед запятой), остаётся прикреплённым к этой же строке кода, как и было.

```jsonc
{
// этот начинался с колонки 0 в исходнике -> прижат к колонке 0
    // этот был с отступом в исходнике -> остаётся на своей строке,
    // но получает обычный отступ
  "name": "Alice", // этот идёт следом за кодом -> остаётся на строке кода
  "age": 30
}
```

Опция `-a, --align-comments` отменяет особый случай для колонки 0: с этим флагом *любой* одинокий комментарий получает обычный отступ, даже если в исходнике он начинался с колонки 0/1.

Опция `--no-comments` полностью запрещает `//` и `/* */` — при их наличии во входных данных это считается синтаксической ошибкой (строгий JSON).

### Компактные массивы и объекты (`--compress`)

Массив, состоящий из **одного** скалярного значения (строка/число/`true`/`false`/`null`), **никогда не разворачивается** в несколько строк — это поведение включено всегда, без ограничения по длине:

```json
{
  "domain": ["geoip:ru"]
}
```

С опцией `-c, --compress` дополнительно не разворачиваются ещё два вида структур — **при условии, что итоговая строка не превышает 150 символов** (считая от её начала):

**1. Массивы скаляров из не более чем 5 элементов:**

```sh
$ echo '{"domain":["domain:ru","domain:su","geosite:vk","geosite:YANDEX"]}' | jsonc_fmt -c
{
  "domain": ["domain:ru", "domain:su", "geosite:vk", "geosite:YANDEX"]
}
```

**2. Объекты из не более чем 5 полей, все значения которых — скаляры:**

```sh
$ jsonc_fmt -c noise.json
{
  "noise": [
    { "rand": "10-30", "randRange": "0-255", "delay": "5-15" },
    { "rand": "20-60", "randRange": "0-255", "delay": "10-25" },
    { "rand": "5-15", "randRange": "0-255", "delay": "1-8" }
  ]
}
```

Здесь внешний массив остаётся развёрнутым (его элементы — объекты, а не скаляры), а каждый отдельный объект — имея не более 5 скалярных полей и укладываясь в 150 символов — остаётся на одной строке.

Без `-c` оба вида структур всегда разворачиваются построчно. С `-c` массив/объект, в котором *больше* 5 элементов, или чьё однострочное представление превысило бы 150 символов, всё равно разворачивается как обычно. Правило применяется только к "плоским" массивам/объектам — без вложенных объектов/массивов и без комментариев внутри; если внутри есть вложенная структура или комментарий, структура всегда разворачивается, чтобы не терять информацию.

### Пустая строка между элементами (`--fine`)

Опция `-f, --fine` вставляет пустую строку между соседними элементами-контейнерами (объектами или массивами) — то есть когда после запятой сразу начинается новый `{` или `[`, а перед запятой был `}` или `]`:

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

### Валидация и ошибки

Перед форматированием `jsonc_fmt` полностью валидирует JSON. Если найдена ошибка, программа **ничего не выводит в stdout**, а печатает в stderr сообщение с точным номером строки и столбца:

```sh
$ echo '{"a": 1, "b": [1,2,] }' | jsonc_fmt
Ошибка: неожиданный токен, ожидалось значение (строка 1, столбец 20)

$ printf '{\n  "a": 1,\n  "b": tru\n}\n' | jsonc_fmt
Ошибка: нераспознанный токен возле 't' (строка 3, столбец 8)

$ printf '{\n  "a": "unterminated\n}\n' | jsonc_fmt
Ошибка: незакрытая строка (перевод строки внутри строки) (строка 2, столбец 21)
```

Обнаруживаются: незакрытые строки/комментарии, недопустимые управляющие символы и escape-последовательности, некорректные числа (`01`, `.5`, `1.` и т. п.), пропущенные `,`/`:`, лишние/висячие запятые, мусор после корневого значения, неожиданный конец файла — с точной позицией для каждого случая.

### Коды возврата

| Код | Значение                                             |
|-----|-------------------------------------------------------|
| 0   | Успех                                                  |
| 1   | Ошибка синтаксиса JSON (сообщение с позицией в stderr) |
| 2   | Ошибка использования (аргументы, файл не открылся и т. п.) |

### Переносимость между разными Linux

Программа не использует ничего специфичного (только стандартный C99 + POSIX `isatty`/`fileno`). Чтобы получить бинарник, независимый от версии glibc целевой системы, соберите статически:

```sh
cc -std=c99 -O2 -Wall -Wextra -static -o jsonc_fmt jsonc_fmt.c
```

Такой бинарник можно скопировать на любую другую Linux-машину той же архитектуры и запустить без установки библиотек.

### Лицензия

Используйте, модифицируйте и распространяйте свободно под лицензией GPL-3.0.
