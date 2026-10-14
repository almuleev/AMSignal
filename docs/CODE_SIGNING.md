# Code signing policy

## Current status / Текущее состояние

Integration for **free SignPath Foundation signing** is prepared. Enrollment,
certificate access and the first signed release are **not yet approved**.
The application was submitted on 2026-10-14 with the owner's authorization;
the official form confirmed receipt. The Foundation's decision is pending.
Existing GitHub downloads and local builds must not be described as signed
unless their Windows Authenticode signature actually verifies.

Подготовлена интеграция бесплатной подписи SignPath Foundation. Одобрение
проекта и подключение сертификата ещё требуются. Настройка скриптов сама по
себе не подписывает уже выпущенные EXE и не убирает предупреждения Windows.

After acceptance, the signing attribution is:
**Free code signing provided by [SignPath.io](https://signpath.io/), certificate
by [SignPath Foundation](https://signpath.org/).** This is a planned provider,
not a claim of an existing sponsorship or a certificate already issued.

## Team and approval

- Author, committer, reviewer and signing approver: repository maintainer
  [almuleev](https://github.com/almuleev), subject to confirmation during enrollment.
- External contributions, including release build scripts, require maintainer review.
- Each production signing request requires manual approval by the maintainer in
  SignPath. An automated build does not approve its own request.
- Maintainers must enable MFA for GitHub and SignPath, as required by the provider.

## Privacy and local system changes

AMSignal does not upload recordings, collect telemetry or contact a signing
service at runtime. Files are read from and exported to destinations chosen
by the user, including explicitly selected network locations. Signing happens
on the release server and sends executable build artifacts, not measurement data.
Windows may independently access certificate/SmartScreen services when verifying
a downloaded executable; that is a Windows security function.

Before running the portable GUI, note that it registers `.AMSig` for the current
Windows user, writes local settings, and can create bounded analysis caches.
File association uses `HKCU\Software\Classes\.AMSig` and
`HKCU\Software\Classes\AMSignal.Project`; no administrator rights are needed.
Removing the portable program folder removes its executables. Settings/caches
may remain; change the `.AMSig` default application in Windows Settings if
needed. Remove the two registry entries manually only if they still refer to
AMSignal and you intend to remove its association. Keep exported recordings.

Программа не отправляет записи и телеметрию. Подпись выполняется на стороне
выпуска и не замедляет FFT/FRF/фильтр. При запуске Windows может проверять
сертификат. До первого запуска учитывайте регистрацию расширения `.AMSig`,
локальные настройки и кэш; это описано выше вместе с удалением программы.

## Activation by the repository owner

1. Publish/review this integration, then apply at
   [SignPath Foundation](https://signpath.org/apply.html), using the ready
   [application draft](SIGNPATH_APPLICATION.md). Acceptance is the provider's
   decision; the public GPLv3 repository meets basic license/visibility conditions.
2. After acceptance, connect the SignPath GitHub App to **almuleev/AMSignal**,
   configure a GitHub trusted build system and a production signing policy
   requiring manual maintainer approval. Use public-trust signing, not a test
   or self-signed certificate. Set up timestamping.
3. Import [signpath-artifact.xml](signpath-artifact.xml). It requires both
   project executables, ProductName `AMSignal`, the identical supplied version,
   exact filenames and SHA-256 Authenticode. Do not sign unrelated binaries.
4. Set repository secret `SIGNPATH_API_TOKEN` to a submitter token. Never put
   a token/private key in source, release files, command history or chat.
5. Set these repository variables to the values supplied by SignPath:

   | Variable | Value |
   |---|---|
   | `SIGNPATH_ORGANIZATION_ID` | Organization ID |
   | `SIGNPATH_PROJECT_SLUG` | Project slug |
   | `SIGNPATH_POLICY_SLUG` | Manually approved production signing policy |
   | `SIGNPATH_CONFIGURATION_SLUG` | Imported artifact configuration slug |
   | `SIGNPATH_ENABLED` | `true`, only after the settings above are complete |

6. Commit/tag the release source on `main`. The tag must contain this
   integration; historical tags predate executable metadata and cannot use it.
   From Actions on **main**, dispatch **Signed release (SignPath Foundation)**
   with that existing tag. It builds and tests on GitHub-hosted Windows runners,
   uploads the unsigned GUI/CLI artifact, then requests signing.
7. Review the source commit, version and build origin in SignPath and manually
   approve the request. The workflow waits up to an hour, then checks trust,
   expected publisher and timestamp on both executables before packaging.
8. The workflow creates a **draft** GitHub release with signed GUI/CLI, portable
   ZIP and SHA-256 checksums of the final signed bytes. Complete/review the release
   notes and publish it. An existing release is never overwritten.

No paid subscription is configured. Public GitHub-hosted runners are used;
do not move this workflow to private/paid runners without reconsidering cost
and provider origin requirements. SignPath approval remains a real prerequisite.

## Local build and verification

Normal `build_cli.ps1`, `build_gui.ps1` and Windows GNU Make builds embed matching
Windows version resources. A local build has **no trusted signature** by default.
The numerical code is unchanged by this integration.

```powershell
./verify_release_binaries.ps1 -InputDirectory signed -Version 0.17.0-rc.4 -RequireSignature
./package_release.ps1 -InputDirectory signed -Version 0.17.0-rc.4 -OutputDir dist/signed/package -RequireSignature -Zip
```

Verification fails for missing/untrusted/untimestamped signatures, an unexpected
publisher, mismatched versions or different GUI/CLI signing certificates. The
unsigned development packaging path is retained and must not be advertised
as a signed release. Do not modify executables after signing.

Signing helps publisher identity and reputation; it does **not** promise immediate
removal of every Edge/SmartScreen warning. See
[Microsoft's explanation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

Provider references: [free-program conditions](https://signpath.org/terms.html),
[GitHub integration](https://docs.signpath.io/trusted-build-systems/github),
[artifact metadata restrictions](https://docs.signpath.io/artifact-configuration/reference#file-metadata-restrictions).
