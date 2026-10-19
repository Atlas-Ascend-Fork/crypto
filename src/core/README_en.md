# core — Runtime and Platform Layer

English | [简体中文](README.md)

The control plane shared by all algorithm families.

Responsibilities:

- Handles, streams, workspaces, and plan caching.
- SoC and implementation-path registration and dispatch.
- Key object management: import, device-resident caching, usage restrictions, and zeroization on destruction.
- Resource quotas for coexistence with AI workloads: stream priorities and core budgets.
- Error codes, logging, common parameter validation, and memory zeroization.

Constraint: no dependencies on specific algorithms.
