# Public Interfaces

English | [简体中文](README.md)

This directory is the only interface boundary for which CANN Crypto promises external compatibility.

Constraints:

- Pure C: no C++ types, exceptions, or templates, and no exposed internal structure members.
- Public descriptors include `structSize` and `version` fields and reserve a `reserved` field for extensions that preserve the ABI.
- No dependency on `aclTensor`. Tensor interfaces are provided by the adapter layer.
