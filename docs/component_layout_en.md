# Component Organization

English | [简体中文](component_layout.md)

Components separate Host and Kernel responsibilities. File counts follow implementation
complexity. The `op_host/` and `op_kernel/` directories organize source code; they do not
require ACLNN registration for direct kernel launch.

```text
src/.../<component>/
├── op_host/          # Validation, tiling, scheduling and Host runtime calls
├── op_kernel/        # Kernel entries, thin launch wrappers and implementation headers
├── tests/
│   ├── ut/op_host/
│   ├── ut/op_kernel/
│   ├── st/
│   └── assets/
├── examples/
├── docs/
├── CMakeLists.txt
├── README.md
└── README_en.md
```

Create directories only when their implementation exists. Public native interfaces reside
in `include/cann_crypto/`. Ordinary Host sources use `.cpp`; existing Kernel `.cpp` files
use CANN build rules, and new ASC translation units may use `.asc`. Moving files does not
require changing compilation modes or merging implementation files.

SHAKE128 and SHAKE256 are two public interfaces within one `shake/` component. They retain
separate kernel entries and tests while sharing the sponge implementation. NTT keeps its
stage, buffer, modular-arithmetic and PQC implementations in separate files. Source-file
counts do not determine operator counts.

The optional PyTorch / torch_npu adapter lives in the top-level `torch_extension/`
directory. It builds separately and reuses the native core, handling tensors, device
contexts, streams and registration; the core builds independently of PyTorch. Add bindings
only for operators that need framework access; neither an adapter directory nor a copy
of an entire project is required for every operator.

See [Tests](../tests/README_en.md) for categories and unified commands. After moving files,
verify test discovery counts and execution results. Changes to device behavior require
validation through the real device path.

Composable device APIs and SDK interfaces require separate design. A common source layout
does not by itself enable embedding an entire operator inside another kernel.
