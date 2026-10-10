# Three person implementation and contribution plan

The group is Anveshaa Kabir Basnet (2405418), Ria Gracy (2405448), and Diksha Nandi (2405493). The assignment below is a proposed division for understanding, reviewing and extending the supplied AI-assisted implementation. It does not claim these tasks have already been completed by any member.

| Member | Own and explain | Suggested actual follow-up contribution |
| --- | --- | --- |
| Anveshaa | server.py HTTP forwarding, sockets, worker admission and shutdown | Add address fallback and a worker saturation regression test |
| Ria | policies.py cache locks/TTL/LRU and limiter, server.py access policy | Improve Cache-Control parsing and add more policy edge cases |
| Diksha | origin.py, benchmark.py, tests, traffic format and docs | Repeat benchmarks, add log summaries and record demo evidence |

## Milestones

1. All members clone and run the local demo; trace one request through the code together.
2. Anveshaa reviews forwarding and malformed request handling. Ria reviews shared state and access policies. Diksha checks every writeup feature against the test suite.
3. Each member creates a feature branch for an actual improvement, makes incremental commits, and opens a pull request. Another member reviews it and runs the checks.
4. Run performance measurements on the evaluation machine. Replace or supplement the example results with clearly labeled measured runs.
5. Rehearse the demo and viva. Every member should explain the full request flow, then discuss their assigned component in depth.

## GitHub workflow

The repository owner adds the other two members as collaborators in repository settings. Each member configures their own Git name/email locally and commits only their own real work.

```sh
git switch main
git pull --ff-only
git switch -c feature/describe-your-change
# Modify the relevant files and run tests.
python -m unittest discover -v
git add path/to/changed/file
git commit -m "Describe the actual improvement"
git push -u origin feature/describe-your-change
```

Open a GitHub pull request from the pushed branch to main. Include the problem, changes and test evidence. Do not create empty commits or attribute generated code to teammates to simulate a contribution history.

## AI usage

The supplied writeup declares AI use for code scaffolding, networking, cache policies, debugging, tests, benchmarks, explanations and documentation. This implementation was generated with AI assistance. Members should review it, acknowledge that assistance in their final submission, and make their own verifiable contributions. Do not claim full authorship of generated code or understanding before reviewing it.
