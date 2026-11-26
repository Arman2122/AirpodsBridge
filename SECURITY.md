# Security Policy

## Supported versions

The [latest public release](https://github.com/Arman2122/AirpodsBridge/releases/latest) receives security fixes. Upgrade to that release to check whether an issue still applies.

## Reporting a vulnerability

Please report privately, through GitHub's
[private vulnerability reporting](https://github.com/Arman2122/AirpodsBridge/security/advisories/new).
Do not open a public issue for a suspected vulnerability.

A useful report includes:

- What an attacker can achieve, and what access they need to start.
- The AirpodsBridge version and Windows version.
- Steps to reproduce, or a proof of concept.
- For Bluetooth, say whether the Control Engine was active and include a redacted `--trace` log if useful.

You can expect an acknowledgement within a week. A fix ships in the next release, and the
advisory is published once it is available.

## Scope

The parts of this project worth attacking, and what is assumed about them:

| Area | Notes |
| --- | --- |
| Bluetooth advertisement parsing | Parses untrusted radio data from anything nearby. Memory-safety bugs here are the highest-severity class in the project. |
| Optional Control Engine | The Windows radio service, adapter setup and recovery, local IPC, and Apple Accessory Protocol parsing. |
| Update checking | Talks to the GitHub API over HTTPS and matches releases against the configured repository. Metadata pointing at another repository is rejected. |
| Installer and portable ZIP | Published unsigned unless code-signing secrets are configured. Verify the SHA-256 digest against `SHA256SUMS.txt`. |
| Settings and logs | Written under `%LOCALAPPDATA%\AirpodsBridge` with the current user's privileges. |

Out of scope:

- Vulnerabilities in Qt, vcpkg packages, or Windows itself. Report those upstream; if a
  fix needs a version bump here, an issue is fine.
- The absence of code signing on unofficial builds.
- Anything requiring an attacker who already has administrator access on the machine.

## A note on what this application reads

Standard mode reads nearby Bluetooth Low Energy advertisements. The optional Control Engine adds two-way communication with the paired device through the [local Windows radio service](Docs/RadioService.md).
