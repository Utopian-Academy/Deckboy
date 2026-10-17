# Utopian Academy community — Discord launch guide

Deckboy is the first public project. The server belongs to **Utopian Academy** so future work can have its own space without moving everybody to a new server.

**Status:** setup guide only. No Discord server or invite is claimed to exist. Do not publish an invite until the server is created, moderated, and tested.

## Server basics

- **Server name:** Utopian Academy
- **Description:** Free tools, curious experiments and people who make things. Home of Deckboy — open-source video cue playback for live shows.
- **Visual identity:** Utopian Academy as the overall server identity, with Deckboy's robot as the first project's recognizable mascot.
- **Invite:** Generate an invitation in Discord once the channels, rules and permissions are ready. Test it with a non-admin account, then add that *real* invitation to the website, README, and support docs.
- **Defaults:** Require email verification; disable @everyone/@here mentions for ordinary members; restrict mass invitations and webhooks; keep moderation logs private; avoid overcomplicated bots.
- **Moderation:** Have at least two trusted moderators when participation grows. Do not publish an email or advertise round-the-clock support until someone actually monitors it.

## Initial channels

```text
UTOPIAN ACADEMY
  #welcome
  #announcements                 (staff posting only)
  #general

DECKBOY
  #deckboy-chat
  #help-and-questions
  #ideas-and-feedback
  #show-and-tell
  #development

THE WORKSHOP
  #other-projects
```

Avoid launching empty channels for future projects. Add categories as real communities form.

## Welcome message (copy into #welcome)

Welcome to **Utopian Academy** ♡

This is a workshop for people who build useful, strange, and wonderful things. Our first project is **Deckboy**: free, open-source video cue playback and show control for live events, theatre, and broadcast.

- New to Deckboy? Try **#deckboy-chat**.
- Need help? **#help-and-questions** is a friendly place to ask.
- Have a workflow suggestion? Use **#ideas-and-feedback**.
- Want to show off a real production or a cool little test? **#show-and-tell**.
- Found a reproducible bug? Please use [GitHub Issues](https://github.com/Utopian-Academy/Deckboy/issues).
- Found a security vulnerability? Report it [privately](https://github.com/Utopian-Academy/Deckboy/security/advisories/new).

Be kind. Don't gatekeep. Never post a client's confidential show files, keys, or sensitive connection details.

Deckboy is free, and that's the point. **Seize the means of playback ♡**

## Community rules

1. Treat new users, veteran engineers and contributors with equal respect. No harassment, slurs, threats or dogpiling.
2. Give constructive critique of software, ideas and workflows, never personal attacks.
3. Do not share passwords, SRT passphrases, stream keys, client show files or other people's private information.
4. No unsolicited promotion, spam, scams or mass direct messages.
5. This server is not staffed emergency show support; there are no guaranteed response times.
6. Keep security reports private using GitHub Security Advisories, not Discord.
7. Respect the Discord Terms and applicable community guidelines.

## Suggested roles

- **Maintainer** — project owner and authorized core collaborators (admin access only where needed)
- **Moderator** — manage moderation, channels and abuse reports
- **Contributor** — optional recognition, not elevated access
- **Member** — default role
- **Deckboy** — optional self-selected interest role, not a permission boundary

## Launch sequence

1. Create the server on the project owner's Discord account.
2. Set up categories, channel permissions, rules and safety settings.
3. Add the welcome message and a short pinned guide to reporting bugs.
4. Test access and post permissions from a non-admin account.
5. Generate a stable invite and set its expiration/use limit deliberately.
6. Add the verified invitation to `docs/community.html`, `SUPPORT.md` and `README.md`.
7. Announce it once on GitHub Discussions. Invite people organically rather than chasing numbers.
