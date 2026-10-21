# Security Statement

English | [简体中文](SECURITY.md)

## Recommended Runtime User

For security, do not run commands as `root` or another administrator account. Follow the principle of least privilege.

## File Permissions

- Set a `umask` of at least `0027` on the host and in containers so newly created directories have permissions no broader than `750` and new files no broader than `640` by default.
- Apply appropriate access controls to sensitive material such as personal data, business assets, source files, and files saved during operator development. For installation directories and shared input data, refer to [Appendix A](#a-recommended-maximum-permissions-for-files-and-directories).
- Operators may cache compiled files in a `kernel_meta_*` directory under the runtime directory to speed up later calls. Control access to these generated files as appropriate.
- **Apply stricter permissions to keys, key components, private keys, certificates, and ciphertext files:** no broader than `700` for directories, `600` for files, and `500` for encryption/decryption scripts. See Appendix A.

## Build Security

Building and installing this project from source produces intermediate files. Control access to them after building.

## Runtime Security

- Write operator invocation scripts with the resources of the runtime environment in mind. If a script exceeds available memory or storage, for example while generating inputs or benchmark results, it may fail and terminate unexpectedly.
- An operator exits and reports an error when it encounters an exceptional condition. Use the error message, synchronous execution when needed, and log files to locate the cause.
- Some components are invoked through PyTorch. Version mismatches may cause runtime failures; see the [PyTorch security statement](https://gitee.com/ascend/pytorch#%E5%AE%89%E5%85%A8%E5%A3%B0%E6%98%8E).

## Cryptographic Security

This project implements cryptographic algorithms and operators. Consider the following when evaluating their security:

- **The Ascend NPU is not a key-protection boundary.** Keys exist in plaintext in device memory and on-chip buffers while in use. This project does not provide a secure enclave or hardware key isolation. Applications requiring keys never to appear in plaintext need an external protection design.
- **Threat model.** The project provides algorithm-level correctness for remote and data-plane attackers. It does not claim protection against malicious processes on the same device, attackers with device-debugging access, physical attacks, or power and electromagnetic side channels.
- **Scope of constant-time behavior.** Implementations involving keys avoid key-dependent branches and memory addresses. This project does not claim resistance to microarchitectural timing side channels; do not assume that it is safe in a multi-tenant environment on that basis.
- **Randomness source.** The device does not provide a true random number generator. Callers must seed deterministic random number generators from a trusted host-side entropy source. Weak seeds directly make keys predictable.
- **Algorithm status.** Algorithms differ in maturity; see the status in the corresponding directory's `README.md`. Passing known-answer tests (KATs) or differential tests does not mean an implementation has any form of cryptographic module certification.
- Report security issues involving cryptographic implementations through the [vulnerability handling process](#vulnerability-handling) below. Do not disclose details in public Issues.

## Public Network Addresses

The following public addresses appear in the project's source:

| Type | Open-Source Code Address | File | Public IP/URL/Domain/Email/Archive Address | Purpose |
| :---: | :---: | --- | --- | --- |
| Dependency | Not applicable | `torch_extension/requirements.txt` | https://download.pytorch.org/whl/cpu | Additional pip index for installing CPU PyTorch as a test comparison dependency |
| In-house | Not applicable | `src/primitives/keccak/cmake/ascend/cmake/util/ascendc_gen_options.py` | http://www.apache.org/licenses/LICENSE-2.0 | Retained Apache-2.0 license URL from the Ascend C operator project template |
| In-house | Not applicable | `src/primitives/keccak/cmake/ascend/cmake/util/parse_ini_to_json.py` | http://www.apache.org/licenses/LICENSE-2.0 | Retained Apache-2.0 license URL from the Ascend C operator project template |

---

## Vulnerability Handling

See the community [vulnerability management policy](https://gitcode.com/cann/community/blob/master/security/security.md).

## Appendix

### A. Recommended Maximum Permissions for Files and Directories

| Type | Maximum Linux Permissions |
| --- | --- |
| User home directory | 750 (`rwxr-x---`) |
| Program files, including scripts and libraries | 550 (`r-xr-x---`) |
| Program file directory | 550 (`r-xr-x---`) |
| Configuration files | 640 (`rw-r-----`) |
| Configuration file directory | 750 (`rwxr-x---`) |
| Log files, completed or archived | 440 (`r--r-----`) |
| Log files being written | 640 (`rw-r-----`) |
| Log directory | 750 (`rwxr-x---`) |
| Debug files | 640 (`rw-r-----`) |
| Debug directory | 750 (`rwxr-x---`) |
| Temporary file directory | 750 (`rwxr-x---`) |
| Maintenance/upgrade file directory | 770 (`rwxrwx---`) |
| Business data files | 640 (`rw-r-----`) |
| Business data directory | 750 (`rwxr-x---`) |
| Directory containing key components, private keys, certificates, or ciphertext files | 700 (`rwx------`) |
| Key components, private keys, certificates, or ciphertext files | 600 (`rw-------`) |
| Encryption/decryption interfaces or scripts | 500 (`r-x------`) |
