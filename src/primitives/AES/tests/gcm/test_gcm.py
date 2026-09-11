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


def _aes128_gcm_reference(
    plaintext_bytes: bytes,
    key16: bytes,
    nonce12: bytes,
    aad_bytes: bytes,
) -> tuple[bytes, bytes]:
    """
    CPU reference implementation using pycryptodome (AES-128-GCM).

    Parameters:
        plaintext_bytes: Plaintext bytes
        key16: 16-byte AES key
        nonce12: 12-byte nonce
        aad_bytes: Additional authenticated data
    """
    try:
        from Crypto.Cipher import AES
    except ImportError:
        pytest.skip("pycryptodome not installed")
    cipher = AES.new(key16, AES.MODE_GCM, nonce=nonce12)
    if aad_bytes:
        cipher.update(aad_bytes)
    ciphertext, tag = cipher.encrypt_and_digest(plaintext_bytes)
    return ciphertext, tag




def test_aes128gcm_interface_exist():
    """Check whether the AES-128-GCM custom operator is registered."""
    logging.debug("Operator binding: %s", torch.ops.ascend_ops.aes_gcm)
    assert hasattr(torch.ops.ascend_ops, "aes_gcm"), \
        "The 'aes_gcm' operator is not registered in the 'torch.ops.ascend_ops' namespace."


PLAINTEXT_LENGTHS = [
    20480,
    40960,
    61440,
    737280,
]


@pytest.mark.skipif(not torch.npu.is_available(), reason="NPU device not found")
@pytest.mark.parametrize("length", PLAINTEXT_LENGTHS)
def test_aes128gcm_operator(length):
    """
    Test the functionality of the AES-128-GCM operator against a CPU reference
    implementation (pycryptodome). Covers various plaintext lengths including
    512-aligned and non-aligned sizes to verify padding and tag correctness.

    Parameters:
        length: Plaintext length in bytes
    """
    # Generate random plaintext, original 16-byte key, 12-byte nonce, and AAD
    plaintext = torch.randint(0, 256, (length,), dtype=torch.uint8)
    key16 = torch.randint(0, 256, (16,), dtype=torch.uint8)
    nonce = torch.randint(0, 256, (12,), dtype=torch.uint8)
    aad = torch.randint(0, 256, (32,), dtype=torch.uint8)

    # CPU reference (using original 16-byte key)
    pt_bytes = plaintext.numpy().tobytes()
    aad_bytes = aad.numpy().tobytes()
    ref_ciphertext, ref_tag = _aes128_gcm_reference(
        pt_bytes, key16.numpy().tobytes(), nonce.numpy().tobytes(), aad_bytes)
    expected_ciphertext = torch.frombuffer(ref_ciphertext, dtype=torch.uint8)
    expected_tag = torch.frombuffer(ref_tag, dtype=torch.uint8)

    # NPU execution (using pre-expanded 176-byte key)
    key_tensor = _prepare_key_tensor(key16.numpy())
    plaintext_npu = plaintext.npu()
    key_npu = key_tensor.npu()
    nonce_npu = nonce.npu()
    aad_npu = aad.npu()
    result_npu = torch.ops.ascend_ops.aes_gcm(plaintext_npu, key_npu, nonce_npu, aad_npu)
    result = result_npu.cpu()

    # The output contains padded ciphertext followed by a 16-byte authentication tag
    aligned_size = (length + 511) // 512 * 512
    if aligned_size % 16 != 0:
        aligned_size = (aligned_size + 15) // 16 * 16

    result_ciphertext = result[:length]
    result_tag = result[aligned_size:aligned_size + 16]

    assert torch.equal(result_ciphertext, expected_ciphertext), \
        f"AES-128-GCM ciphertext failed for length={length}. " \
        f"Expected first 16 bytes: {expected_ciphertext[:16].tolist()}, " \
        f"but got: {result_ciphertext[:16].tolist()}"

    assert torch.equal(result_tag, expected_tag), \
        f"AES-128-GCM tag failed for length={length}. " \
        f"Expected tag: {expected_tag.tolist()}, " \
        f"but got: {result_tag.tolist()}"

    logging.info("Test passed: length=%s", length)