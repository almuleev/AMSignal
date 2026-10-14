# SignPath Foundation application draft

**Draft only — not submitted.** Confirm the owner's contact and service terms
before submitting. Certificate access is not yet granted. Do not claim approval.

| Field | Proposed value |
|---|---|
| Project | AMSignal |
| Repository/homepage | https://github.com/almuleev/AMSignal |
| License | GNU GPLv3; https://github.com/almuleev/AMSignal/blob/main/LICENSE |
| Download page | https://github.com/almuleev/AMSignal/releases |
| Maintainer/approver | almuleev — https://github.com/almuleev |
| Contact email | Confirm with the owner; existing public security contact is `al.muleev@gmail.com` |
| Code signing policy | https://github.com/almuleev/AMSignal/blob/main/docs/CODE_SIGNING.md — must be published first |
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

## Owner confirmations needed

- Ownership/control of the GitHub repository and the contact address.
- MFA enabled on GitHub and SignPath; approver identity and review responsibility.
- Acceptance of the free-program conditions, including SignPath Foundation being
  displayed as signing publisher and manual production approval.
- Installing/authorizing the provider's GitHub App for this repository.

Apply: https://signpath.org/apply.html. This file does not send an application,
create an account, accept terms, configure secrets or claim a certificate.
