These rules are read at run time from the trusted base branch. Edit this file
to change how findings are rated and how reviewers are chosen; no recompile is
needed.

### Rating (1–5)

Rate each verified finding as `rating = clamp(severity + occurrence, 1, 5)`.

**Severity** is the impact if the bug triggers:

| Severity | Score | Meaning |
|---|---|---|
| Cosmetic | 1 | Typos, wording of log messages or comments, naming, formatting, dead code, with no effect on behavior |
| Minor | 2 | Wrong but harmless or easily recoverable behavior, such as a misleading log message or error code, or a wrong result in a rarely used edge case with an easy workaround |
| Bug | 3 | Incorrect behavior in a supported flow: a driver, service or library call that fails or misbehaves while the system keeps running, a memory, handle or mapping leak on an error path, a build break on one supported configuration, a regression of existing behavior |
| Severe | 4 | A hang, deadlock or lost interrupt or wakeup, a leak on a frequently used path that exhausts memory or handles over time, a driver or service left unusable until reboot, storage or file system data loss or corruption, a break of the syscall or IPC protocol ABI |
| Critical | 5 | Kernel panic or fault, a memory-safety bug (out-of-bounds access, use-after-free, double free, uninitialized pointer or size), a DMA or IOMMU mapping that lets a device access memory it should not, a user program that can read or write kernel or other processes' memory or bypass handle or capability checks, a system that no longer boots on a supported platform |

**Occurrence** is whether the bug can happen in the real world:

| Occurrence | Adjustment | Meaning |
|---|---|---|
| Natural | +0 | Reachable during normal boot or operation on a supported platform (amd64, i386, generic AArch64, Raspberry Pi) in QEMU or on real hardware, or by any user program through syscalls or IPC |
| Uncommon | −1 | Needs specific hardware or firmware, a non-default build option, a narrow race window, memory exhaustion, or a real but rare device or hardware failure |
| Synthetic | −2 | Only reachable through host test harnesses or mocks, states the surrounding code makes impossible, or bad input from a component that is already fully trusted (for example the kernel passing bad data to itself) |

User space is untrusted. Syscall arguments, user pointers and IPC messages can
be crafted by any program, so a bug reachable that way is natural even if no
current program triggers it.

For every finding, state the concrete trigger scenario that justifies the
occurrence class. If you cannot describe a trigger, the finding is synthetic.

Other rules:

- Findings you could not confirm by reading the code are discarded, not rated.
- Findings in code the PR did not change (and does not make newly reachable) are
  discarded.
- In an incremental review, findings in code the new commits did not change (and
  do not make newly reachable) are also discarded; an earlier review covered it.
- Style, naming and documentation findings based on `GUIDELINES.md` or the
  `AGENTS.md` checklists are out of scope for this review and are discarded.
- Agreement between hunters is shown in the report but never changes the rating.
- Do not inflate ratings to make the report look useful. A PR with no real
  findings is a good outcome.

### Human reviewer

Request a human reviewer only when at least one of the following holds:

- a finding is rated 4 or 5;
- two or more findings are rated 3 or higher;
- a finding rated 3 or higher is in sensitive code: memory management
  (`kernel/memory/`), syscalls (`kernel/api/`, `syscalls.table`), interrupts,
  scheduling and synchronization (`kernel/interrupts/`, `kernel/threads/`,
  `kernel/sync/`, `kernel/arch/x86/interrupts/`, the AArch64 GIC and exception
  code in `kernel/arch/aarch64/common/`), handles and capabilities
  (`kernel/handles/`, `kernel/capabilities.c`), DMA and IOMMU
  (`kernel/memory/dma/`, `kernel/arch/aarch64/smmu/`, `librt/libddk/dma/`),
  or boot code (`boot/`).

Otherwise do not request anyone and say why in the report.

Choose reviewers **only from the repository's CODEOWNERS file**:

1. Read `.github/CODEOWNERS` from the workspace (it is restored from the base
   branch and therefore trusted). Do not use a `CODEOWNERS` from any other
   workspace location, since that would come from the PR. If you need to check
   `CODEOWNERS` or `docs/CODEOWNERS`, fetch it from the base branch with the
   GitHub tools. If none exists, request nobody and state that no CODEOWNERS
   file was found.
2. Match with GitHub semantics: patterns follow `.gitignore` rules, the **last**
   matching pattern wins, and `#` starts a comment. Ignore e-mail owners.
3. Prefer the owners of the files that contain the highest-rated findings. If
   those have no owner, fall back to the owners of the most heavily changed files.
4. Request at most two reviewers. Prefer individual users over teams.
5. Never request the PR author, anyone already requested, and anyone who has
   already reviewed the PR.
6. Never request anyone who is not listed in CODEOWNERS.

### Report format

Post a single comment:

1. The review scope: "Full review" or "Incremental review of N new commits",
   with the reason from the scope file when there was an earlier review, and
   links to the compare view and the previous review when available.
2. A one-line summary: number of findings per rating and which hunters
   contributed (`claude`, `gpt`, `grok`; note any that failed).
3. A table of findings sorted by rating, highest first, with columns: rating,
   location (`path:line`), title, severity, occurrence, found by.
4. For each finding rated 3 or higher: what is wrong, the concrete trigger
   scenario, the impact and a suggested fix.
5. Findings rated 1 or 2 inside a collapsed `<details>` block, one line each.
6. A "Human review" section: the decision, the reason, and the reviewers
   requested (or why none).
7. A line explaining how to re-request: apply the `ai-review` label again. After
   new commits only those are reviewed; without new commits the whole PR is
   reviewed again.

If there are no verified findings, say so briefly and still include the human
review decision.
