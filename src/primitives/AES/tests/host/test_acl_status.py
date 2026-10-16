# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.

from pathlib import Path
import re
import shutil
import subprocess


def test_acl_sync_status_is_propagated_on_cpu(tmp_path):
    compiler = shutil.which("c++")
    assert compiler is not None, "a C++ compiler is required for the AES host status test"

    test_dir = Path(__file__).resolve().parent
    aes_root = test_dir.parents[1]
    source = test_dir / "test_acl_status.cpp"
    binary = tmp_path / "test_acl_status"
    ctr_source = (aes_root / "csrc/ctr/dav-2201/ctr.asc").read_text(encoding="utf-8")
    gcm_source = (aes_root / "csrc/gcm/dav-2201/gcm.asc").read_text(encoding="utf-8")
    sync_call = re.compile(
        r"CheckAclSynchronize\(\s*aclrtSynchronizeStream,\s*stream,\s*ACL_SUCCESS,\s*"
        r'"AES-128-(?:CTR|GCM)[^"]+"\s*\)'
    )
    assert len(sync_call.findall(ctr_source)) == 1
    assert len(sync_call.findall(gcm_source)) == 4
    assert "aclrtSynchronizeStream(stream);" not in ctr_source
    assert "aclrtSynchronizeStream(stream);" not in gcm_source

    subprocess.run(
        [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
        check=True,
        capture_output=True,
        text=True,
    )
    result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
    assert "ACL_SYNC_STATUS_RESULT=PASS" in result.stdout
