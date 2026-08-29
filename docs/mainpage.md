# libzxc API Reference

ZXC is a lossless data compression library built for high decompression
throughput and random access. An archive is a sequence of independently
decodable blocks, optionally indexed by a seek table, so any block can be
decompressed without reading the blocks before it.

This is the generated API reference. For the project overview, benchmarks and
installation instructions see the
[README](https://github.com/hellobertrand/zxc#readme); for the wire format see
the [format specification](https://github.com/hellobertrand/zxc/blob/main/docs/FORMAT.md).

## Choosing an API

- \ref buffer_api — one-call compression of a whole buffer. Start here.
- \ref context_api — reuse one context across many operations to avoid
  repeated allocation.
- \ref static_context_api — caller-provided workspace, no heap allocation.
- \ref block_api — single blocks, without the archive header and footer.
- \ref stream_api and \ref pstream — incremental pull and push streaming.
- \ref seekable_api — random access to any block of a seekable archive.
- \ref dictionary — pre-trained dictionaries for small, homogeneous payloads.

## Reference

- \ref error — error codes and their meaning.
- \ref levels — compression levels.
- \ref block_size — block-size codes and limits.
- \ref file_format — on-disk format constants.
- \ref library_info and \ref version — build and version queries.
- \ref threading — thread-count limits.
