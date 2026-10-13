# Contributing Guide

English | [简体中文](../../CONTRIBUTING.md)

We welcome developers to try this project and contribute to it. Before contributing, see [cann-community](https://gitcode.com/cann/community) for the code of conduct, sign the CLA, and become familiar with the source repository's contribution process.

> For illustrated GitCode instructions covering forks, clones, SSH keys, and PR creation, see the [GitCode workflow guide](https://gitcode.com/cann/community/blob/master/contributor/gitcode-workflow.md). Project-specific requirements are described here.

## Before You Begin

1. **Sign the CLA.** If you have not signed, or your commit email differs from the signed email, the robot adds `cann-cla/no` and the PR cannot be merged. See the [CLA guide](https://gitcode.com/cann/infrastructure/blob/master/docs/cla/cla%E4%BD%BF%E7%94%A8%E6%8C%87%E5%8D%97.md).
2. **Complete the PR template.** Explain the background, purpose, and approach of your change.
3. **Discuss new algorithms, public interfaces, or directory-layer changes in an Issue first.** This helps avoid rejection after implementation. If you are unsure whether a change is a simple bug fix, open an Issue to discuss it.
4. **Disclose the use of generative AI tools.** The PR template asks for the agent platform and version, model and version, and a prompt summary. See the [AI coding assistant policy](https://gitcode.com/cann/infrastructure/blob/master/docs/strategy/ai-coding-assistants.md). An incomplete declaration receives the `ai-compliance-failed` label.

## Contribution Scenarios

- [Contribute a new algorithm or primitive](#1-contribute-a-new-algorithm-or-primitive)
- [Fix a bug](#2-fix-a-bug)
- [Improve performance](#3-improve-performance)
- [Correct documentation](#4-correct-documentation)
- [Help with someone else's Issue](#5-help-with-someone-elses-issue)

---

## 1. Contribute a New Algorithm or Primitive

### 1.1 Choose the Code Layer

The project is organized in dependency layers. Before adding code, identify the appropriate layer:

| Directory | Contents | Typical Contribution |
| --- | --- | --- |
| `src/math/` | Mathematical engine: big integers, modular arithmetic, NTT/RNS, finite fields | Mathematical operations shared across algorithm families |
| `src/primitives/` | Cryptographic primitives: AES, SM4, `Keccak-f`, ChaCha, etc. | Algorithm kernels that are not complete secure services on their own |
| `src/algorithms/` | Algorithm services: hash, AEAD, signature, KEM, etc. | Complete cryptographic services exposed to callers |

Dependencies flow only downward. `src/algorithms/` and `src/primitives/` do not implement mathematical operations themselves; shared operations belong in `src/math/`. Algorithm families do not depend on one another.

If placement is unclear, explain the proposal in an Issue and have SIG members confirm it before starting development.

### 1.2 Create a Requirement Issue

Create a `Requirement|需求建议` Issue. It should generally include:

- **Background:** what problem the algorithm or primitive solves.
- **Value:** applicable scenarios and benefits relative to existing implementations.
- **Design:** interface shape, lower-layer dependencies, and parallelization approach.

See the [Issue guide](https://gitcode.com/cann/community/blob/master/contributor/issue-operation.md) for the procedure.

### 1.3 Review the Requirement

Add the proposal to the SIG agenda and join a crypto SIG meeting for review.

- [Agenda and minutes](https://etherpad-cann.meeting.osinfra.cn/p/sig-crypto)
- [Meeting link](https://meeting.osinfra.cn/cann/)
- Regular meeting: 16:00–17:30 Beijing time on the first Thursday of each month.

For an urgent requirement, request an ad hoc SIG meeting: add the request to the agenda and email a [maintainer](https://gitcode.com/cann/community/blob/master/CANN/sigs/crypto/README.md) with “Request for an ad hoc SIG agenda item” and the topic in the subject line.

### 1.4 Submit a PR

#### Deliverables

```text
${component}                          # Component name, such as chacha20_poly1305
├── ${component}_internal.h           # Internal interface with neutral types
├── ${component}_launch.cpp           # Kernel composition and variant selection
├── op_host/                          # Operator definition, InferShape, and Tiling
├── op_kernel/                        # Ascend C implementation
├── tests/                            # Unit tests and reference comparisons
├── docs/                             # Interface and build documentation
├── examples/                         # Invocation examples (optional)
├── CMakeLists.txt
└── README.md                         # Required
```

`op_api/` is optional. For algorithms with opaque keys, variable-length batches, or per-item status returns, tensor semantics may be unnatural; a direct aclnn entry point is not required. Its absence means direct aclnn calls are not yet supported.

#### Requirements

| Type | Requirement |
| --- | --- |
| Code | Kernel implementation, Host-side Tiling, and unit tests |
| Documentation | A component `README.md` is required and must state algorithm status and implemented parameter sets |
| Correctness | Known-answer tests (KATs) and differential tests against an independent reference implementation |
| Copyright | New source files must have a copyright and license header; see below |

#### Copyright Header

New source files must contain:

```text
Copyright (c) <year> Huawei Technologies Co., Ltd.
Licensed under CANN Open Software License Agreement Version 2.0.
```

If a file is based on a template or third-party code with another license notice, **retain that notice**. Describe the source in the Issue so SIG members can determine whether the third-party introduction process applies.

#### Compliance Checklist

- [ ] Code follows the [C++ coding standards](https://gitcode.com/cann/community/blob/master/contributor/coding-standards/C++%20Coding%20standards.md).
- [ ] Code follows the [secure coding standards](https://gitcode.com/cann/community/blob/master/contributor/coding-standards/SecureCoding.md).
- [ ] Code builds successfully.
- [ ] Implementations involving keys have no key-dependent branches or memory access patterns.
- [ ] No undeclared third-party snippets are introduced; see the [snippet reference guide](https://gitcode.com/cann/community/blob/master/contributor/third_party/snippet-reference-guideline.md).
- [ ] Markdown documentation follows syntax conventions.

#### Commit Conventions

- Create a development branch from the upstream repository's latest `master`, push it to your fork, and submit a PR targeting `cann/crypto:master`.
- Use a clear PR title and a description of the change and its reason, and link the relevant Issue.
- **Keep one topic per PR.** Do not mix feature work with a broad directory reorganization.
- If a PR has multiple commits, the robot adds `stat/needs-squash`; squash them into one commit and force-push.

### 1.5 CI Gate

Comment `/compile` on the PR to start the gate, then address its results. Current checks include:

- Code compilation.
- Static checks (CodeCheck, CodeCheck_style, precommit).
- Open-source compliance scans (SCA, Antipoison).
- PR checks (empty files, binary files, etc.).

The gate has a 15-minute time limit. Device-independent repository-level tests under `tests/ut/common/`, `tests/ut/op_api/`, and `tests/ut/op_host/` belong in the gate; the `tests/ut/op_kernel/` layer requires Ascend hardware, so its full matrix and performance regressions belong in daily builds.

### 1.6 Committer Review

After the gate passes, mention the relevant [Committers](https://gitcode.com/cann/community/blob/master/CANN/sigs/crypto/README.md) in a PR comment and respond to their feedback promptly.

Changes under `src/math/` are shared by multiple algorithm families, and stable interfaces are difficult to change. A Maintainer responsible for that layer must participate in their review.

### 1.7 Maintainer Merge

The robot merges a PR after it has `cann-cla/yes`, `lgtm`, `approved`, and `ci-pipeline-passed`.

**PR authors cannot approve their own PRs.** A new commit invalidates reviews made before it, so reviewers must comment again.

---

## 2. Fix a Bug

1. Describe the behavior, reproduction steps, and environment in an Issue.
2. A simple fix can be submitted as a PR linked to the Issue. Discuss interface or behavior changes in an Issue before implementing them.
3. **For correctness bugs, add a test case that reproduces the problem** to prevent regressions.

## 3. Improve Performance

1. Explain the opportunity, expected benefit, and validation method in an Issue.
2. Include before-and-after measurements in the PR, with the environment, SoC model, batch size, and message length.
3. **Separate “data already on device” from “including host–device transfers”** in performance results; their conclusions often differ.
4. State the CPU baseline model, thread count, and compiler options, and enable available cryptographic extensions.

## 4. Correct Documentation

For an error, unclear wording, or broken link, you can submit a PR directly without opening an Issue first.

## 5. Help with Someone Else's Issue

You are welcome to answer questions in the [Issue list](https://gitcode.com/cann/crypto/issues). If the answer requires code changes, submit a PR linked to the Issue.

---

## Appendix

### Related Links

| Resource | Link |
| --- | --- |
| Illustrated GitCode workflow | [gitcode-workflow.md](https://gitcode.com/cann/community/blob/master/contributor/gitcode-workflow.md) |
| Code of conduct | [code-of-conduct.md](https://gitcode.com/cann/community/blob/master/contributor/code-of-conduct.md) |
| CLA guide | [CLA guide](https://gitcode.com/cann/infrastructure/blob/master/docs/cla/cla%E4%BD%BF%E7%94%A8%E6%8C%87%E5%8D%97.md) |
| PR guide | [pull_request_operation.md](https://gitcode.com/cann/community/blob/master/contributor/pull_request_operation.md) |
| Robot commands | [Robot guide](https://gitcode.com/cann/infrastructure/blob/main/docs/robot/robot%E4%BD%BF%E7%94%A8%E6%8C%87%E5%8D%97.md) |
| AI coding assistant policy | [ai-coding-assistants.md](https://gitcode.com/cann/infrastructure/blob/master/docs/strategy/ai-coding-assistants.md) |
| crypto SIG | [CANN/sigs/crypto](https://gitcode.com/cann/community/tree/master/CANN/sigs/crypto) |
| Security statement | [SECURITY.md](SECURITY.md) |
