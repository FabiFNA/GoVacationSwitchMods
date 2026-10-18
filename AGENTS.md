# Notes for contributors and AI assistants

This file is the starting point for anyone who wants to work on this repository. That includes people and AI coding assistants.

The repository holds two mods for GO VACATION on Switch in Ryujinx and a tool that unpacks the model archives of the game. The README explains how to use them. The docs folder explains how they work.


## Rules

1. No game files and no game code. Never add files from a dump. Never add instructions or data copied from the game. Addresses and names of functions are fine.
2. The patches for main may only contain instructions we compute ourselves. Hooks redirect calls. They never copy or replay an instruction of the game. See docs/hooks.md.
3. No keys and nothing that helps to decrypt the game.
4. No personal data. No user names and no paths of your own machine in code or docs or commit messages.
5. Never commit the folders exefs and out and tools and plugin/build. The gitignore already excludes them.
6. Texts for users follow one style. No commas. No hyphens. No emojis. Short simple sentences. This applies to the README and the docs and release texts. Code comments are free.


## Where to start

1. docs/architecture.md: how the plugin gets into the game and how the two mods differ
2. docs/hooks.md: how calls are redirected and how to add a new hook
3. docs/addresses.md: every address the code uses
4. docs/features.md: how each feature of the mod menu works
5. docs/debugging.md: logs and crashes and how to find the cause
6. docs/formats.md: the WTA archives and DISC.DAT


## Build and check

```
python plugin/build.py
```

This builds both mods into the out folder. With install it also copies them into Ryujinx. Put the ExeFS of your own dump into the exefs folder to let the build check every patched call against your main.

After a change test both mods in Ryujinx. Only one of them may be enabled at a time.
