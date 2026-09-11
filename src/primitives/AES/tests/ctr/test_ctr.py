#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import logging

import torch
import torch_npu
import ascend_ops
import pytest


from tests.aes_common import _prepare_key_tensor


def _aes128_ctr_reference(plaintext_bytes: bytes, key16: bytes, nonce12: bytes) -> bytes:
    """
    CPU reference implementation using pycryptodome.

    Parameters:
        plaintext_bytes: Plaintext bytes
        key16: 16-byte AES key
        nonce12: 12-byte nonce
    """
    try:
        from Crypto.Cipher import AES
    except ImportError:
        pytest.skip("pycryptodome not installed")
    cipher = AES.new(key16, AES.MODE_CTR, nonce=nonce12, initial_value=1)
    return cipher.encrypt(plaintext_bytes)




def test_aes128ctr_interface_exist():
    """Check whether the AES-128-CTR custom operator is registered."""
    logging.debug("Operator binding: %s", torch.ops.ascend_ops.aes128ctr)
    assert hasattr(torch.ops.ascend_ops, "aes128ctr"), \
        "The 'aes128ctr' operator is not registered in the 'torch.ops.ascend_ops' namespace."


PLAINTEXT_LENGTHS = [
    1,
    15,
    16,
    17,
    64,
    128,
    256,
    511,
    512,
    513,
    1023,
    1024,
    1025,
    2048,
    4096,
    8192,
    10000,
    20000,
    50000,
    100000,
]


@pytest.mark.skipif(not torch.npu.is_available(), reason="NPU device not found")
@pytest.mark.parametrize("length", PLAINTEXT_LENGTHS)
def test_aes128ctr_operator(length):
    """
    Test the functionality of the AES-128-CTR operator against a CPU reference
    implementation (pycryptodome). Covers various plaintext lengths including
    512-aligned and non-aligned sizes to verify padding correctness.

    Parameters:
        length: Plaintext length in bytes
    """
    plaintext = torch.randint(0, 256, (length,), dtype=torch.uint8)
    key16 = torch.randint(0, 256, (16,), dtype=torch.uint8)
    nonce = torch.randint(0, 256, (12,), dtype=torch.uint8)

    pt_bytes = plaintext.numpy().tobytes()
    ref_bytes = _aes128_ctr_reference(pt_bytes, key16.numpy().tobytes(), nonce.numpy().tobytes())
    expected = torch.frombuffer(ref_bytes, dtype=torch.uint8)

    key_tensor = _prepare_key_tensor(key16.numpy())
    plaintext_npu = plaintext.npu()
    key_npu = key_tensor.npu()
    nonce_npu = nonce.npu()
    result_npu = torch.ops.ascend_ops.aes128ctr(plaintext_npu, key_npu, nonce_npu)
    result = result_npu.cpu()

    result_valid = result.flatten()[:length]

    assert torch.equal(result_valid, expected), \
        f"AES-128-CTR failed for length={length}. " \
        f"Expected first 16 bytes: {expected[:16].tolist()}, " \
        f"but got: {result_valid[:16].tolist()}"

    logging.info("Test passed: length=%s", length)