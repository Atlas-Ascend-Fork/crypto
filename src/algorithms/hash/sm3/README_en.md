# SM3 device batch digest

`CannCryptoSm3` computes SM3 digests for equal-length messages in device memory on Ascend 910B. The caller supplies the device buffers and ACL stream. The function waits for completion on that stream.

See the [build and example commands](README.md) and [API contract](docs/api.md).
