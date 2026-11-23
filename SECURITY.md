# Security

Please report vulnerabilities in OdinGuard (protocol, crypto, parsers, server module, Studio/updater) privately
through GitHub's "Report a vulnerability" (Security tab) instead of a public issue.

Out of scope: generic "a determined attacker with admin rights can bypass the client" reports – that is a known,
documented limit (docs/02-architecture.md, section 11). In scope: anything that lets a remote party break the
encryption, forge server data (policy, signature packages, releases, updates), crash or take over the server or the
guard through crafted input, or get honest players banned.

Never publish your server's keys (`server.seed`, `signing.seed`, `build.secret`, updater key).
