# Contributing

Thanks for helping. Short version:

- Open an issue before large changes. For a new board, include photos of the PCB
  markings and the pin map you verified.
- Keep firmware changes small and testable. Run `pio run` for every board env
  before opening a PR.
- Screen format changes must update `schema/screen.schema.json` and, if breaking,
  bump `schema` (the integer in each screen file). Old versions must keep loading.
- Python: `ruff` clean, type hints on public functions. Bash: `shellcheck` clean.
- Every source file starts with `SPDX-License-Identifier: Apache-2.0`.
- Sign off your commits (`git commit -s`); this certifies the
  [Developer Certificate of Origin](https://developercertificate.org/). Contributions
  are licensed under Apache-2.0.
- Never commit credentials, Wi-Fi passwords, API keys, or private certificates.

By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).
