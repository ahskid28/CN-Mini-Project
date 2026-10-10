# Three person contribution plan

| Member | Roll | C code to own and explain | Suggested next contribution |
| --- | --- | --- | --- |
| Anveshaa Kabir Basnet | 2405418 | platform.h, connect_origin, HTTP parser and workers | Improve total deadlines and add saturation tests |
| Ria Gracy | 2405448 | CacheEntry, cache_get/cache_put, blocked, rate_allowed | Improve Cache-Control token parsing and add policy edge cases |
| Diksha Nandi | 2405493 | log_event, test harness, benchmark, CI and documentation | Repeat measured benchmarks and add CSV analysis |

These responsibilities are proposed, not a claim of existing individual authorship. The C implementation was prepared with AI assistance. Every member should build and run it, trace buffer ownership and understand the complete request flow.

1. Run the demo and integration suite together.
2. Review the assigned functions and identify a real improvement.
3. Create a member-owned feature branch, commit incremental actual changes, open a pull request and have another member review it.
4. Rerun benchmark measurements on the evaluation machine and label them with platform and workload.
5. Rehearse the viva, including sockets, HTTP framing, locks, memory ownership, TTL/LRU and sliding windows.

The owner adds collaborators through GitHub settings. Each member commits under their own configured Git identity. Do not generate empty commits or false attribution to simulate contribution history. The source writeup declares AI use across implementation, tests, debugging and documentation; acknowledge this assistance honestly in the final submission.
