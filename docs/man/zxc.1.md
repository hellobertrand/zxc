# zxc(1)

## NAME
**zxc** - High-performance asymmetric lossless compression

## SYNOPSIS
**zxc** [*OPTIONS*] [*INPUT-FILE*] [*OUTPUT-FILE*]

## DESCRIPTION
**zxc** is a command-line interface for the ZXC compression library, a high-performance lossless compression algorithm optimized for maximum decompression throughput.

**zxc** is designed for the *"Write Once, Read Many"* paradigm. It trades compression speed to generate a bitstream specifically structured to maximize decompression speed, effectively offloading complexity from the decoder to the encoder. It aims to provide very high decompression speeds across modern architectures while maintaining competitive compression ratios.
ZXC is particularly suited for scenarios such as Game Assets, Firmware or App Bundles where data is compressed once on a build server and decompressed millions of times on user devices.

By default, **zxc** compresses a single *INPUT-FILE*. If no *OUTPUT-FILE* is provided, **zxc** will automatically append the `.xc` extension to the input filename. If no *INPUT-FILE* is provided, **zxc** will read from standard input (`stdin`) and write to standard output (`stdout`).


## STANDARD MODES

**-z**, **--compress**
: Compress FILE. This is the default mode if no mode is specified.

**-d**, **--decompress**
: Decompress FILE.

**-l**, **--list**
: List archive information, including compressed size, uncompressed size, compression ratio, and checksum method.

**-t**, **--test**
: Test the integrity of a compressed FILE. It decodes the file and verifies its checksum (if present) without writing any output.

**-b**, **--bench** [*N*]
: Benchmark in-memory performance. Loads the input file entirely into RAM and measures raw algorithm throughput (default duration is 5 seconds).

## OPTIONS

**-m**, **--multiple**
: Process multiple files at once. When specified, all subsequent non-option arguments are treated as input files. For each input file, a corresponding `.xc` file is created (or decompressed into its original name). Output cannot be written to standard output (`stdout`) when this mode is enabled.

**-1**..**-5**
: Set the compression level from 1 (fastest compression) to 5 (highest density).
- **-1, -2 (Fast):** Optimized for real-time assets or when compression speed is a priority.
- **-3 (Default):** Balanced middle-ground offering efficient compression and superior ratio to fast codecs.
- **-4, -5 (Compact):** Best choice for distribution, archival or firmware.

**-T**, **--threads** *N*
: Set the number of threads to use for compression and decompression. A value of `0` means auto-detection based on the number of available CPU cores.

**-C**, **--checksum**
: Enable block hashing during compression using the rapidhash algorithm. Recommended for data integrity validation. Checksum verification is automatically performed during extraction when enabled.

**-N**, **--no-checksum**
: Explicitly disable checksum generation.

**-k**, **--keep**
: Keep the input file after compression or decompression. (Currently, the input file is preserved by default, but this flag ensures compatibility with future changes).

**-f**, **--force**
: Force overwrite of the *OUTPUT-FILE* if it already exists.

**-c**, **--stdout**
: Force writing to standard output (`stdout`), even if it is the console.

**-v**, **--verbose**
: Enable verbose logging mode. Outputs more detailed information during operations.

**-q**, **--quiet**
: Enable quiet mode, suppressing all non-error output (such as progress bars or real-time statistics).

**-j**, **--json**
: Output results in JSON format. This is particularly useful for scripting, benchmarking, and the `--list` mode.

## SPECIAL OPTIONS

**-V**, **--version**
: Display the version of the zxc library and the compiled architecture information, then exit.

**-h**, **--help**
: Display a help message and exit.

## EXAMPLES

**Compress a file:**
  zxc data.txt

**Compress a file with high density (Level 5):**
  zxc -5 data.bin

**Decompress a file:**
  zxc -d data.txt.xc

**Compress multiple files independently:**
  zxc -m file1.txt file2.txt file3.txt

**Decompress a file to standard output:**
  zxc -dc data.txt.xc > data.txt

**List archive information:**
  zxc -l data.txt.xc

**Run a benchmark for 10 seconds:**
  zxc -b 10 data.txt

## AUTHORS
Written by Bertrand Lebonnois & contributors.

## LICENSE
BSD 3-Clause License.
