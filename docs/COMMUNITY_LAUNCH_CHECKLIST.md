# Utopian Academy / Deckboy — community launch checklist

**Working plan, not a claim that accounts have been opened.**

Utopian Academy is the umbrella community. Deckboy is its first public project. Neither software access nor joining a community requires a donation.

## What can go live with this PR

- [x] Deckboy community landing page: `docs/community.html`
- [x] GitHub support guide, contributor guidance, issue links and PR template
- [x] Community rules / code of conduct
- [x] Discord server blueprint for Utopian Academy
- [x] Links to the community landing page from the homepage, press kit, FAQ and README

## Requires project-owner account access

- [ ] **Domain:** confirm ownership and registrar access to `utopianacademy.cool`; register it first if it is unowned. Avoid naming a working public email until DNS and mail delivery are configured.
- [ ] **Email:** establish a real send-and-receive mailbox at `utopia@utopianacademy.cool`. The local part of an email address is best published consistently in lowercase.
- [ ] **DNS:** follow the selected mail provider's domain verification, MX, SPF and DKIM records; add DMARC. Never copy another provider's sample MX records.
- [ ] **Test:** send a message both to and from the address using an unrelated mailbox, check that replies come from the project address, and verify that messages don't land in spam.
- [ ] **Aliases (optional):** create `press@`, `security@` or `hello@` only if useful; point them at the monitored mailbox and test each.
- [ ] **Discord:** create the Utopian Academy server, add Deckboy channels from `docs/DISCORD_SETUP.md`, configure rules and moderation, and test as a regular member.
- [ ] **Invite:** generate and test a stable Discord invite, then add it to `docs/community.html`, `SUPPORT.md` and `README.md`.
- [ ] **Publish contact:** only after the mailbox works, add a `mailto:` link to the community page and press kit; update `SECURITY.md` if vulnerability reports will actually be monitored at a dedicated mailbox.
- [ ] **Announcements:** post the community invitation in GitHub Discussions and update the project profile or social bios as appropriate.

## Suggested inexpensive mail approaches

**A full mail account (recommended for professional outreach):** use a custom-domain email host that supports incoming mail and authenticated sending. For low-volume projects, services like Migadu or Zoho Mail offer inexpensive small plans; verify current pricing, limits and the provider's exact DNS instructions before purchase.

**Free receiving-only alternative:** Cloudflare Email Routing can forward incoming messages to an existing inbox when the domain uses Cloudflare DNS. It does **not** let you reply *from* the project domain. Don't mistake an inbound alias for a complete public-contact mailbox.

Avoid running your own public mail server purely to save a few dollars: deliverability, filtering, DNS reputation and security are ongoing maintenance obligations.

## Keep it simple

No crowdfunding buttons, sponsor appeals, paid tiers or gated Discord channels. The only call to action is participation: try Deckboy, ask a question, report a bug, share a workflow, help with docs, or tell another operator.
