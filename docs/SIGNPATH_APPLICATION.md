# SignPath Foundation application — submitted

**Submitted on 2026-10-14 at approximately 18:40 MSK (15:40 UTC).**
The owner explicitly authorized merging PR #1 and sending this application as
Aleksandr Muleev with contact `al.muleev@gmail.com`, including the free-program
terms and personal-data processing. The official application page confirmed
**“Form submitted — Thank you, we'll be in touch soon.”** Marketing consent
was left unchecked. Certificate access and enrollment approval are still pending.

PR #1 was merged after all Windows/Linux CI checks passed, at commit
`0c565af0e8f5ea20cd6605f535680dac9a781773`. The form linked the published
download/policy section at https://github.com/almuleev/AMSignal#code-signing-policy
and the privacy section in docs/CODE_SIGNING.md. Selected category: Science;
maintainer: Individual maintainer(s); build system: GitHub Actions. Reputation
was reported accurately: 50 published releases, 75 aggregate release-asset
downloads, 0 stars and 0 forks as of submission; no broad-adoption claim.

The text below records the prepared application background. The actual form
used a shorter description of the same project and purpose. Next steps require
the provider's decision, GitHub App authorization, MFA and real SignPath
configuration. Do not claim a signed release before verified signed EXE exist.

| Field | Submitted project / integration context |
|---|---|
| Project | AMSignal |
| Repository/homepage | https://github.com/almuleev/AMSignal |
| License | GNU GPLv3; https://github.com/almuleev/AMSignal/blob/main/LICENSE |
| Download page | https://github.com/almuleev/AMSignal#code-signing-policy |
| Maintainer/approver | almuleev — https://github.com/almuleev |
| Contact email | `al.muleev@gmail.com`, explicitly confirmed by the owner |
| Code signing policy | https://github.com/almuleev/AMSignal/blob/main/docs/CODE_SIGNING.md — published before submission |
| Build workflow | `.github/workflows/release-signpath.yml`, GitHub-hosted Windows/UCRT64 |
| Artifacts | Native GUI `AMSignal-<version>-x64.exe` and CLI `lvm_reader.exe` |

## Description for the application

AMSignal is a maintained, publicly released GPLv3 native Windows application for
engineering measurement data. It reads LabVIEW LVM, TXT/CSV and AMSignal project
files and provides time-domain plots, FFT spectra and H1/Welch or Direct frequency
response analysis with coherence. It supports formulas, digital filters,
annotations, PNG/data exports and adaptive RAM/disk analysis of large recordings.
GUI and CLI share the project's C++17 parsing/analysis core.

We request the free SignPath Foundation open-source signing service for both
project executables, distributed as standalone EXE files and a portable ZIP on
GitHub Releases. Builds will originate from reviewed release tags on main using
GitHub-hosted runners; each signing request will require manual maintainer
approval. Windows product metadata and restrictive artifact configuration are
provided. No measurement data is submitted to the signing service.

The project uses the system Win32/GDI libraries and the MinGW/GCC C++ runtime.
There is no paid proprietary application component in the documented build.
Please review the repository and advise on enrollment requirements and eligibility.

## Enrollment follow-up

- Owner contact and application submission were confirmed; wait for the provider's reply.
- MFA enabled on GitHub and SignPath; approver identity and review responsibility.
- Free-program terms were accepted with owner authorization, including SignPath
  Foundation as signing publisher and manual production approval.
- Installing/authorizing the provider's GitHub App for this repository.

Application endpoint: https://signpath.org/apply.html. This document records the
confirmed submission; it does not configure secrets or claim certificate access.
