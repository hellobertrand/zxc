# zxc(1)

## NAME
**zxc**, **unzxc**, **zxccat**, **zxcless**, **zxcgrep** - Compress or decompress .zxc files

## SYNOPSIS
**zxc** [*OPTIONS*] [*INPUT-FILE*] [*OUTPUT-FILE*]

**unzxc** is equivalent to **zxc -d**.

**zxccat** is equivalent to **zxc -dcf** applied to every *INPUT-FILE* in turn, writing them all to stdout; input that does not start like a ZXC archive (see **-f**) comes out unchanged.

**zxcless** [**--dict=***FILE*] [*LESS-OPTIONS*] [*FILE*...] shows files, or stdin, in **less**(1), decompressing ZXC archives on the fly; other input displays unchanged. **--dict** supplies the dictionary of archives that need one (see **-D**). The **ZXC** environment variable names the **zxc** binary to use.

**zxcgrep** [**--dict=***FILE*] [*GREP-OPTIONS*] [**-e**] *PATTERN* [*FILE*...] runs **grep**(1) on files, or stdin, decompressing ZXC archives on the fly; other input is searched unchanged. **--dict** supplies the dictionary of archives that need one (see **-D**). Options go before the pattern; those that select files (**-r**, **-d**, **-D**, **--include**, **--exclude**) and **-f -** are not supported. The exit status is grep's: 0 if a line matched, 1 if none did, 2 on any error, including a damaged archive. **ZXC** and **GREP** name the binaries to use; grep must support **--label** (GNU and BSD grep do).

**unzxc** and **zxccat** are symlinks to **zxc**, and **zxcless** and **zxcgrep** are shell scripts; all four are installed on POSIX systems only.

## DESCRIPTION
**zxc** is the command-line interface to the ZXC compression library, a lossless compressor designed for maximum decompression throughput.

ZXC follows the *write once, read many* model: it spends more effort on compression to produce a bitstream that decodes as fast as possible, moving the complexity from the decoder to the encoder. It targets very high decompression speed on modern architectures while keeping competitive compression ratios, and suits data such as game assets, firmware or application bundles that is compressed once on a build server and decompressed many times on user devices.

By default **zxc** compresses a single *INPUT-FILE*. Without *OUTPUT-FILE*, the output name is the input name with `.zxc` appended on compression, or with `.zxc` removed on decompression. Without *INPUT-FILE*, **zxc** reads stdin and writes stdout.


## STANDARD MODES

**-z**, **--compress**
: Compress *INPUT-FILE*. This is the default mode.

**-d**, **--decompress**
: Decompress *INPUT-FILE*. This is the default mode when **zxc** is invoked as **unzxc**; an explicit mode option still takes precedence.

**-l**, **--list**
: List archive information: compressed size, uncompressed size, compression ratio, checksum and seek table (`enabled`, `disabled`, or `mixed` across concatenated frames), dictionary ID (if any) and frame count; the file name is shown when several files are listed. Also accepts a `.zxd` dictionary file, in which case it prints the dictionary's `dict_id`.

**--train**
: Train a dictionary from the input *FILE*s given as training samples. The output path is set with **-o** (see **--output**); when **-o** is omitted, the dictionary is written to `dictionary_<dict_id>.zxd` in the current directory. An existing file there is replaced only with **-f**. See **DICTIONARIES**.

**-t**, **--test**
: Check the integrity of *INPUT-FILE*: decode it and verify its checksums, if any, without writing output. A valid archive prints `OK` and exits 0. With **-v**, the report also states whether the checksum was verified, absent, skipped by **-N**, or unknown for input read from a pipe, whose header cannot be re-read. Under **-j**, `checksum_verified` is true only when a checksum was actually checked, and `checksum` takes the same values as **-l**: `enabled`, `disabled`, `mixed` or `unknown`.

**-b**, **--bench** [*N*]
: Benchmark in-memory performance: load *INPUT-FILE* entirely into RAM and measure raw compression and decompression throughput for *N* seconds (default 5).

## OPTIONS

**-m**, **--multiple**
: Process several files at once: every non-option argument is an input file, each compressed to its own `.zxc` file or decompressed to its original name. Cannot be combined with **-c** or **-o**.

**-r**, **--recursive**
: Process directories recursively: every regular file under a directory given as argument is handled in place. Compression skips `.zxc` files; decompression and **-t** take only those. Existing outputs are overwritten only with **-f**. Symbolic links are reported and skipped, the root included; a trailing `/` names its target instead. Implies **-m**.

**-1**..**-7**
: Set the compression level, from 1 (fastest compression) to 7 (smallest output). Default: 3. Higher levels trade compression speed for ratio; decompression stays fast at every level. Levels 6 (density) and 7 (ultra) compress far more slowly and suit data written once and read many times.

**-T**, **--threads** *N*
: Set the number of threads for compression and decompression. `0` (the default) auto-detects it from the available CPU cores.

**-B**, **--block-size** *SIZE*
: Set the compression block size. *SIZE* must be a power of two between **4K** and **2M** (e.g. `4K`, `64K`, `256K`, `512K`, `1M`). Smaller blocks use less memory and improve random access; larger blocks generally compress better. The default is **512K**, tuned for bulk and archival workloads. Only meaningful on compression: the block size is stored in the archive header and reused on decompression.

**-C**, **--checksum**
: Checksum every block with rapidhash during compression, and verify the checksums during decompression. This is the default.

**-N**, **--no-checksum**
: Disable checksums: none is written on compression, and none is verified on decompression or under **-t**.

**-D**, **--dict** *FILE*
: Use a pre-trained dictionary (`.zxd`) for compression or decompression. On compression, the archive records the dictionary's `dict_id` in its header. On decompression, **-D** is **required** for any archive that was compressed with a dictionary: there is no auto-lookup, and without it decompression fails with a dictionary-required error (see **DICTIONARIES**).

**-o**, **--output** *FILE*
: Write output to *FILE*. For compression and decompression this overrides the positional *OUTPUT-FILE* (single-file mode only); when omitted the output name is derived from the input. For **--train** it sets the dictionary path: if *FILE* is a directory (or ends with a path separator) the dictionary is saved inside it as `dictionary_<dict_id>.zxd`, otherwise *FILE* is used verbatim; when omitted the dictionary is written to `dictionary_<dict_id>.zxd` in the current directory.

**-S**, **--seekable**
: Append a seek table to the archive during compression. The archive then supports random access: the decoder locates and decompresses any block in `O(1)` without reading the whole file. Suited to compressed filesystems, game assets and log analysis.

**-k**, **--keep**
: Keep the input file. This is the default behavior; the flag overrides **--rm**.

**--rm**
: Remove the input file(s) once compression or decompression has succeeded. Ignored when writing to stdout or to a file named with **-o** or *OUTPUT-FILE*, and with **-t** or **-l**. A symbolic link given as input is itself removed; its target is left intact.

**--no-preserve**
: Do not copy the input's attributes to the output. By default, when the input is a regular file, the output takes its permission bits (no setuid, setgid or sticky), access and modification times, group and, as root, owner; decompression thus restores the original's. The output is created `0600` and gets its final mode once complete; a group that cannot be kept loses its permission bits. If the mode or times cannot be set, **zxc** warns and keeps the input. On Windows only the times are copied. The format stores none of these.

**-f**, **--force**
: Overwrite *OUTPUT-FILE* if it exists. An existing file or symbolic link is removed and a new file created, so a link at the output path is replaced, never followed; a device such as `/dev/null` is written as is. When decompressing to stdout (with **-c**, or from stdin without an *OUTPUT-FILE*), also copy input that is not a ZXC archive unchanged. A named file is decoded only if it starts with the 4-byte magic word. stdin cannot be rewound, so there only its first byte counts: input that starts with `0xF5`, which never starts UTF-8 text, is decoded as an archive.

**-c**, **--stdout**
: Write to stdout, even when it is a terminal; compressed data to a terminal needs **-f**.

**-v**, **--verbose**
: Print more information during operations.

**-q**, **--quiet**
: Suppress all non-error output, such as progress bars and statistics.

**-j**, **--json**
: Print results as JSON, for scripting, benchmarking and **-l**.

**--progress** *MODE*
: Control the progress display: **auto** (default) shows a progress bar on stderr when it is a terminal and the file is larger than 1 MB; **always** reports progress even when stderr is not a terminal (one line per update, at most once per second) or when the size is unknown (stdin); **never** disables it. **-q** suppresses progress regardless of this option.

## SPECIAL OPTIONS

**-V**, **--version**
: Print the version of the zxc library and the compiled architecture, then exit.

**-h**, **--help**
: Print a help message and exit.

## DICTIONARIES

For workloads compressed in small blocks (4K–128K), a pre-trained dictionary can dramatically improve the compression ratio. The dictionary prefills the LZ77 window at the start of every block, so the benefit is per-block: the smaller the block, the more it relies on the dictionary for early matches.

Dictionaries are external `.zxd` files referenced from the archive header by a 32-bit `dict_id` (a hash of the dictionary content). The `.zxd` extension is cosmetic; a `.zxd` file is identified by its magic word, not its name.

When **--train**'s **-o** targets a directory (or is omitted, defaulting to the current directory), **zxc** names the file `dictionary_<dict_id>.zxd` (the `dict_id` is the lowercase 8-digit hex reported by `zxc -l`). On decompression the dictionary is **not** auto-located: an archive compressed with a dictionary must be decompressed by supplying that dictionary with **-D**, otherwise decompression fails with a dictionary-required error rather than producing corrupt output.

## CONCATENATED ARCHIVES

Archives can be concatenated (`cat a.zxc b.zxc > ab.zxc`): **-d** decodes them one after the other into one output, and **-t** checks every one. Each keeps its own block size, checksum, seek table and dictionary; **-l** reports the frame count and `mixed` where they differ; with **-v** or **-j** it lists every block size and dictionary ID instead. **-D** supplies one dictionary: frames compressed with another fail with a dictionary-mismatch error.

## EXAMPLES

**Compress a file:**
  zxc data.txt

**Compress a file and remove the original:**
  zxc --rm data.txt

**Compress a file with high density (Level 6, archival):**
  zxc -6 data.bin

**Compress a file with maximum density (Level 7, Ultra):**
  zxc -7 data.bin

**Decompress a file:**
  zxc -d data.txt.zxc

**Decompress a file using the unzxc alias:**
  unzxc data.txt.zxc

**Print the contents of several archives, one after the other:**
  zxccat part1.zxc part2.zxc | grep ERROR

**Read a compressed log page by page:**
  zxcless app.log.zxc

**Search compressed logs, with file names and line numbers:**
  zxcgrep -n ERROR logs-*.zxc

**Compress multiple files independently:**
  zxc -m file1.txt file2.txt file3.txt

**Compress all files in a directory recursively:**
  zxc -r ./my_folder

**Decompress all files in a directory recursively:**
  zxc -d -r ./my_folder

**Decompress a file to stdout:**
  zxc -dc data.txt.zxc > data.txt

**List archive information:**
  zxc -l data.txt.zxc

**Compress with a custom block size (64 KB):**
  zxc -B 64K data.bin data.zxc

**Compress with maximum block size (2 MB):**
  zxc -6 -B 2M data.bin data.zxc

**Run a benchmark for 10 seconds:**
  zxc -b 10 data.txt

**Train a dictionary into a directory (saved as `dictionary_<dict_id>.zxd`):**
  zxc --train -o dicts/ samples/*.json

**Compress with a dictionary using small blocks:**
  zxc -B 4K -D dicts/dictionary_bc46eec1.zxd input.json

**Decompress (the dictionary is required, pass it with -D):**
  zxc -d -D dicts/dictionary_bc46eec1.zxd input.json.zxc

## BUGS
Report bugs at <https://github.com/hellobertrand/zxc/issues>.

## AUTHORS
Bertrand Lebonnois

## LICENSE
BSD 3-Clause License.
