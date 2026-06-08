# Captain context — mod-ale-chronicle (fork)

> Voir aussi [.capy/CAPY-PLATFORM.md](./CAPY-PLATFORM.md).

## What this is

Fork of the ALE (AzerothCore Lua Engine) module. It provides the Lua scripting/bridge layer consumed by the Chronicle WoW 3.3.5a narrative private server — the Lua side of the strict ingame/out-of-game frontier, relaying events to Chronicle's Elixir `narrative_service` and never holding business logic or prompts.

This fork is an **AzerothCore module**: it is compiled into the server build (built and hosted elsewhere). Do not attempt to build the server here.

Keep divergence minimal. Chronicle-specific patches belong here only when they cannot be expressed downstream through config/data or accepted upstream.

## Branches

| Branch | Role |
|---|---|
| `master` | Default branch; upstream-sync baseline mirroring the ALE upstream |
| Feature branches | Chronicle-specific patches or process/docs changes isolated from upstream sync commits |

## Consumed by

- The Chronicle AzerothCore server stack (built/hosted elsewhere) loads this module via the standard module mechanism.
- Ultimate consumer: [`NomenAK/Chronicle`](https://github.com/NomenAK/Chronicle).

## Upstream sync strategy

- Pull opportunistically: needed features, build/runtime fixes, CVEs, or explicit human request; no blind fixed-schedule sync.
- Keep upstream merge/cherry-pick commits separate from Chronicle-specific patch commits.
- Prefer contributing or cherry-picking broadly useful changes upstream instead of carrying private divergence.

## Hard rules

1. **DO NOT modify upstream code** unless the patch is Chronicle-specific and impossible elsewhere. Prefer downstream config/data first, then upstream contribution or cherry-pick.
2. **Lua bridge stays a relay** — no LLM calls, no prompts, no narrative business logic in this module; all of that lives in Chronicle's Elixir service.
3. **Commit messages** follow upstream convention (Conventional Commits where applicable).
4. **Secrets** follow [.capy/CAPY-PLATFORM.md §1](./CAPY-PLATFORM.md#1-vm-capy--secrets). Never persist tokens or private config in this repo.
5. **HTTP calls must have explicit timeouts** (see upstream httplib timeout fix); no unbounded client reads.
6. **Do not build the server here** — server build/runtime validation happens downstream in the Chronicle stack.

## CI

- Server build/integration validation happens downstream through the Chronicle stack, not in this fork.
- For docs/process-only changes, verify changed files and skip expensive C++ builds unless requested.

## Linear / coordination

Team/project context: `Omega · Chronicle`. Keep generic Linear CLI/auth guidance in [.capy/CAPY-PLATFORM.md §4](./CAPY-PLATFORM.md#4-linear-coordination).

## Local agent note

`.capy/` is fork-local agent context. It intentionally avoids root-level agent instruction files so upstream syncs have fewer predictable conflicts. No `.capy/BUILD.md` for this fork.

## Related repos

- Ultimate consumer: https://github.com/NomenAK/Chronicle
- AzerothCore core fork: https://github.com/NomenAK/azerothcore-wotlk-chronicle
- Sibling module fork: https://github.com/NomenAK/mod-playerbots-chronicle
