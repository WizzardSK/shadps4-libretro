# Rules for coding agents

These apply to any AI agent working in this repository (Claude Code reads them through `CLAUDE.md`). They came out of testing the libretro cores with their testers.

## Branches and history

- The `libretro` branch is never deleted, and nothing that rewrites its history is run on it: no force-push, no rebase, no amending or squashing of commits that are already pushed. It is the fork's only long-lived branch.
- New features and experimental changes go on their own branch, and testers test builds of that branch. When testing is done, the related commits are squashed and merged into `libretro`, so the main branch does not collect commits that were superseded midway.
- A fix that users of the `libretro` builds need before the branch is merged is cherry-picked into `libretro`, in a way that does not break the later merge of the branch.

## Merging upstream shadPS4

This repository builds the libretro core and nothing else. The standalone's parts - its entry point, packaging files, tests, documents, upstream's CI and its contributor files - were deleted on purpose, and must not come back with a merge. The paths are listed in `.upstream-excluded`.

- Upstream is shadps4-emu/shadPS4, branch `main` (remote `upstream`). Only full merges of it, no cherry-picked upstream commits.
- Right after `git merge` of upstream, before resolving anything else, run:
  `git rm -r -q --ignore-unmatch --pathspec-from-file=.upstream-excluded`
  This settles the modify/delete conflicts in favour of the deletion, and it also removes files upstream newly added under those paths, which git would otherwise bring in without any conflict.
- Never resolve a conflict on one of those paths by restoring the file.
- When upstream changes a build file around one of the removed parts, keep them removed and take the rest of the change.
- When something new is deleted for the same reason, add its path to `.upstream-excluded` in the same commit.
- This `AGENTS.md`, `README.md` and `CHANGELOG.md` replace or add to upstream's own. When a merge conflicts on them, keep these.
- Changes to the emulator's own files stay behind `#ifdef LIBRETRO` or `if (ENABLE_LIBRETRO)`, and as small as they can be, so merges stay easy.
- After a merge, `CHANGELOG.md` names the upstream commit merged.

## These files

- Only the developer writes to or deletes `AGENTS.md` and `CLAUDE.md`. An agent does not change them on its own; it proposes the change to the developer instead.

## Changelog and README

- `CHANGELOG.md` is updated in the same commit as the change it describes: what changed for someone using the core, newest first. Not a copy of the commit message.
- `README.md` is kept true in the same way: when a change makes a sentence in it wrong (a limit lifted, an option added, a file moved), fix the sentence in that commit.

## Code

- Keep comments true. When a change makes a comment describe behaviour that no longer exists, fix or remove the comment in the same commit.
- In the emulator's own files follow upstream's style and its `.clang-format`, and comment only what the libretro core changes and why. The core's own files in `libretro/` are commented as the other cores are.
- Look at the big picture, not just the function being changed.
- No game-specific hacks, as upstream asks: a fix addresses the emulation's behaviour, not one title.
- Every core option has to be connected to something. Do not add an option the core does not read, and remove one that turns out to do nothing. Option defaults follow the standalone's defaults unless there is a reason, written down beside the option.
- Option texts never mention other emulators or cores.
- A setting the frontend's user should reach is a core option, set on top of the emulator's configuration when a game starts; the emulator's own config files stay the place for everything else.

## How the core is built

- The emulator runs in a process of its own, started by the core (`libretro/libretro_core.cpp` in the frontend's process, `libretro/host.cpp` in the emulator's). Everything that crosses between them goes through the shared memory of `libretro/shared_state.h`. Do not move emulator code into the frontend's process: the emulator needs its address range to itself and can only be stopped by ending its process.
- A change to the layout of `SharedState` bumps `SHARED_VERSION`.

## Testing

- The emulator runs the game's code on an x86-64 CPU. On ARM (aarch64) the core runs the x86-64 emulator through box64, which it carries; a change can be tried there, but say which of the two it was tried on.
- The core logs, at start, its version and the commit it was built from. Ask testers for the frontend's log and the emulator's own (`system/shadPS4/log/shadps4.log`), and check which commit a log came from before drawing conclusions from it.
- Compare with standalone shadPS4 at the same upstream version before calling something a core bug; when standalone fails the same way, it is upstream's.
- Cores are built by CI, not locally.

## Libretro pitfalls already hit in the other cores

Each of these was a bug in at least one of the cemu, rpcs3, vita3k, xenia or xemu cores. Check new code against them before asking testers.

- Audio: each retro_run hands the frontend exactly one frame's worth (sample rate / declared fps), never "whatever is queued". Several streams are mixed, not appended one after another, and each is taken in its own format and sample rate.
- Geometry: the size the frame is handed over at goes to the frontend: base geometry at load, SET_GEOMETRY when it changes. The max has to cover the highest output resolution, and no more than that: a frontend sizes its shader passes by it.
- Files: everything the core writes goes under system/shadPS4/ (or saves/); nothing next to the frontend's executable.
- No window: the standalone's UI has no window in a core. Every path that reaches for the window or its UI thread needs a headless branch that answers the way the user most likely would.
- Unload: never end the frontend's process. Closing content ends the emulator's process, with a time limit, and never waits without one.
- No downloads at run time; data files ship with the core.
