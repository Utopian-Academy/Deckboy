# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in Deckboy, please **do not** open a
public GitHub issue.

Instead, report it privately through [GitHub Security Advisories](https://github.com/Utopian-Academy/Deckboy/security/advisories/new), using the "Report a vulnerability" option on the Security tab. Please do not disclose a vulnerability in GitHub Discussions, Discord or public issues.

A project contact address will be listed here once the mailbox is verified.

We will acknowledge your report within 48 hours and aim to provide a fix or
mitigation within 7 days for critical issues.

## Scope

Deckboy is a desktop application that is deliberately network-active (NDI, OSC,
SRT, RTMP, NMOS, Companion, etc.). Security reports related to any of the
following are in scope:

- Remote code execution via crafted media files or network input
- Buffer overflows or memory corruption in the native code
- Vulnerabilities in bundled dependencies (FFmpeg, SDL3, etc.)
- Unintended data exfiltration (Deckboy collects no telemetry by design)

## Supported Versions

| Version | Supported |
|---|---|
| Latest release | ✅ |
| Older releases | Best effort |

## Acknowledgments

We appreciate responsible disclosure and will credit reporters in the release
notes (unless you prefer to remain anonymous).
