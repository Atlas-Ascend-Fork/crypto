# Benchmarks

English | [简体中文](README.md)

Required dimensions:

- **Batch size sweep**: 1 / 64 / 1K / 64K, etc.
- **Message length sweep**: short and long messages often lead to opposite conclusions.
- **Every SoC and every dispatch path**: testing only the default path is insufficient.

## Organization

- `micro/`: individual operators or primitives.
- `end_to_end/`: complete scenarios.

The [repository PR pipeline configuration](../.gitcode/workflows/crypto-actions.yml) defines the CI scope. It currently lists code checks, x86 compilation, Markdown checks, and op_host unit tests explicitly; it does not define benchmark regression jobs or a daily schedule. Scheduling the full test matrix and performance regressions requires separate configuration.
