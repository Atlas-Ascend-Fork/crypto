# SM3 设备批量摘要

`CannCryptoSm3(input, output, batch, inputLength, stream)` 在调用者提供的 ACL stream 上同步计算一批等长设备消息。每条输出占 32 字节。实现支持 Ascend 910B，包含标量及跨消息 SIMD 路径。

在已加载 CANN 环境的 Linux 上，从仓库根目录执行：

```bash
cmake -S src/algorithms/hash/sm3 -B build-sm3 -DCANN_BUILD_DEVICE=ON -DCANN_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-sm3 --parallel
ctest --test-dir build-sm3 --output-on-failure --no-tests=error
cmake --install build-sm3 --prefix "$PWD/install-sm3"
cmake -S examples/sm3 -B build-sm3-example -DCMAKE_PREFIX_PATH="$PWD/install-sm3"
cmake --build build-sm3-example
./build-sm3-example/sm3_consumer
```

接口约定见 [API](docs/api.md)。
