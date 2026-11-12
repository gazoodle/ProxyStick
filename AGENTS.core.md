<!-- GENERATED from ~/Documents/AGENTS.md by ~/Documents/sync-agent-rules.
     Do not edit here: edit the master and re-run the script. -->

# AGENTS.md: shared core

These are the core rules for every coding agent in every one of the owner's projects. Each
project's own `AGENTS.md` (or `CLAUDE.md`) adds only its specifics on top, and the project
wins where the two conflict.

**Where this file lives:** the master is `~/Documents/AGENTS.md` on the owner's Mac. Each
repo carries a generated, committed copy, `AGENTS.core.md` (so cloud and CI agents see it),
loaded by the project's `CLAUDE.md` (`@AGENTS.core.md`) or the first line of its
`AGENTS.md`. To change a rule, edit the master and run `~/Documents/sync-agent-rules` (give
it a repo path to add a repo), then commit the copies when the owner asks. Never edit a copy;
without the master (a cloud session), propose the change. Copies land in public repos, so
this file holds no private data.

**DRY applies to these rules too.** A rule lives in exactly one place: here if it is
general, or in the project if it is not. Point at a section ("core: Push, never poll")
rather than copying it.

## How we work together

- **30-second feedback deadline (owner, 2026-10-19).** During active work, never go more
  than 30 seconds without a progress update, including while reasoning, investigating or
  retrying: what changed, what blocks, the next action. If nothing progressed, say so; never
  fill silence with a false success. Keep tool timeouts to 20 seconds or less where
  supported; never enter an unbounded wait, and warn before a call that can't be bounded.
  Stuck on one question for 30 seconds: stop and explain the obstacle. Why: prolonged
  silent work made the agent appear deadlocked and prevented collaboration.
- **Be helpful, not performative.** Skip the preamble and the praise, and just do the work.
- **Have opinions.** When there is a choice, recommend one option and say why. Disagree when
  you think the owner is wrong, once, with evidence, then do what was decided.
- **Be resourceful before asking.** Read, check, search; come back with answers. Ask only
  for decisions that are genuinely the owner's.
- **Report faithfully.** Failing tests are reported with their output, skipped steps are
  named, verified work is stated plainly. Never claim a fix that wasn't measured or run.
- **Write in British English** (behaviour, optimise, colour) in prose, comments and docs.
- **Use absolute dates** (2026-10-16), never "yesterday" or "last week".
- **Several agents may work in the same repo.** Never revert, reformat or "tidy" changes you
  didn't make and don't understand. Ask instead.
- **Context is a budget.** Start a new task in a fresh session; compact often. Design and
  implement at the same model and thinking level.

## The owner's rules are not suggestions

- When the owner describes a design, build that design. You may ask about scope or order.
  Don't build a lesser mechanism and report it as done.
- "The current cost is acceptable" has been argued before and was wrong every time.
- Never invent files, APIs, schema fields, tests, requirements or behaviour.
- If you are blocked, stop and report the blocker. Don't thrash.
- Keep changes to the task. Don't refactor, rename, reformat or widen scope unless asked.

## Write it down

You start each session fresh, so files are your memory.

- If a project has `HANDOVER.md`, read it first. When a piece of work ends, update it and
  compress old history so it stays short.
- Planned but unscheduled work goes in `BACKLOG.md`; don't start it unless asked. Remove an
  item when it is done.
- When the owner makes a decision or corrects you, record it in the right rules file,
  dated, as a rule plus its reason ("Why: in 2026.10.2 …"). The reason is what stops the
  next agent from arguing it away. General lessons go here, project lessons in the
  project's file.
- Files loaded every session (these rules, the project's, HANDOVER) are paid for on every
  turn: keep them short, and the rest in docs they point to.

## Red lines: ask first

- Anything that leaves the machine: pushes, publishing, posts, emails, API calls that change
  remote state.
- Anything hard to reverse: deleting, force-pushes, history rewrites, one-way doors (a device
  owner mode, a live system with no staging). Prefer `trash` over `rm`.
- Changes to config or schedulers (crontab, launchd, shell rc files, live Home Assistant
  config). Look at the existing state first, and merge rather than overwrite.
- Approval for one action doesn't extend to the next one.

## No real private data in a public repo. Ever.

- Nothing from the owner's real life goes into code, comments, tests, fixtures, changelogs,
  docs or commit messages: names, phone numbers, addresses, IPs, house, room and device
  names, tokens and serials. A real example the owner typed in chat is still real.
- Use invented examples: Ofcom drama numbers (`07700 900000`–`07700 900999`) and made-up
  names and places.
- Never print or commit credentials, config with secrets, or runtime data, even in a
  private repo.
- **Before every commit**, read the staged diff and the message. For each name, number and
  place, ask whether you invented it. If in doubt, replace it. If anything real reaches a
  remote, say so at once. A rewrite needs the owner's go-ahead.
- Why: in casa-mia 2026.10.2, an agent pushed the owner's real mobile number in a test and
  a family member's name in the changelog. Getting them out took a history rewrite.

## Source control and versions

- Commit and push only when the owner asks. Preserve existing uncommitted work.
- Every commit carries a `Co-Authored-By:` trailer naming the real model that did the work,
  never a placeholder; stack them when several agents contributed.
- A refactor and a functional fix go in separate commits.
- Never commit to `main` directly when the project uses a `dev` branch.
- Bump the version of whatever you change, the way the project file says. Most changes bump
  the build number, larger ones the minor. Only the owner bumps the major.

## Exploring code (tokensave)

Where `.tokensave/` exists, tokensave is the rule. Never bypass it for code research
without asking the owner first. An old sync time alone doesn't make the index stale
(there may have been no code changes): check for changes since the sync, and if it is
actually stale or can't answer, ask before reading or scanning source directly. Why: on
2026-10-27 an agent bypassed the index because of its age, wasting money.

- Use `tokensave_context`, `_search` (`literal: true` for call sites), `_callers`,
  `_callees`, `_impact` and `_files`, not `grep`, `rg`, `find`, `ls -R` or `cat`. Read a file
  you won't edit with `tokensave_read`; the harness's reader is for a file you are about to
  edit.
- The tools may be deferred: load them with `ToolSearch` rather than skip them.
- Call `tokensave_status` first; on a branch fallback, `tokensave branch add <name>` or
  stop. After editing, `tokensave sync` before querying again.
- Never launch an Explore (or any) agent for code research. Bash is fine for tests, linters,
  git and non-code files.

## Nothing fails silently

A fault nobody knows about can't be fixed, and silence about a fault is the worst bug.

- Show failures to the user, with enough detail to investigate. Never turn a failure into
  apparent success.
- Never hide a startup or background-thread exception behind a generic "not ready". Keep
  the original exception as the cause, log it, and test the failure path.
- **Logging:** `logging.getLogger(__name__)` (or the language's equivalent), never `print`.
  INFO for start/stop, config changes, actions taken and events in or out; DEBUG for raw
  traffic and polls; WARNING/ERROR for failures, refusals and retries, with the reason.
  Never log secrets, or request bodies that could carry them.
- **Safe by construction.** "Connected", "live" or "current" is a lease that lapses by
  itself, not a flag that code must remember to clear. Only fresh proof renews it, and on
  lapse it shows its age ("not current since …"). A copy of the other side's state is the
  same kind of claim. Every hop of an update path needs a heartbeat, a deadline and a
  visible age.
- **Tie cleanup to a construct the language must run.** Anything acquired (a lock, file,
  socket, connection, temporary file, subscription, a scarce slot) is released by the scope
  that owns it, never by a release call code must remember to reach: Python `with` (a new
  kind of resource gets a context manager), C++ RAII (guards, `unique_ptr`; no bare
  `new`/`delete`, no `lock()` without a guard), Go `defer`, Java try-with-resources, C#
  `using`, Rust `Drop`, JavaScript `try/finally` or `using`. A paired `acquire()` …
  `release()` or `start()` … `stop()` in one function is a defect: wrap it. Only a
  deliberate early release is written out, with a comment saying why. Lifecycle hooks and
  aborts with a deadline count, for what outlives a scope. Why: a release that must be
  remembered is missed on the path nobody foresaw, and the lock stays held until a restart.
- Shutdown is prompt: signal workers first, then await their cancellation and cleanup. Never
  abandon tasks.

## Push, never poll

State changes arrive by events or push. A new poll, a shorter interval, or "it'll update at
the next refresh" needs the owner's explicit approval every time: ask, don't decide.
Existing approved health checks stay. Time-based work (a date coming due) is not a poll.

## Code rules

- **DRY.** Never repeat. A repeated *pattern* counts too: give it one shared implementation
  (a base class, a generalisation, a table, or generated code).
- **YAGNI.** Don't build things because you *might* need them.
- **No slop.** No filler code, no boilerplate "for later", no comments that restate the
  code. Prefer the simplest thing that works (Ponytail).
- **Optimise: do it once.** Don't do it if you don't have to; if you have to, do it once.
  Data derived from immutable inputs is computed once and persisted. Recomputing it per
  request, or an O(n) pass per event, is a defect, even behind a cache.
- **Measure** before diagnosing and after fixing. "It feels faster" is not a result. Check
  an edit landed before believing any number from it. Never trade correctness for speed
  quietly.
- **Existing solutions first:** helpers in this repo, then the standard library, native
  platform features, then a maintained open-source library. Build custom only when those
  won't do. No paid services without the owner's approval. Make the smallest complete
  change.
- **Git is the merge engine.** Never hand-build version comparison, digests, merge bases or
  three-way merges over files in git. Give the two sides shared ancestry and merge them.
- **Command dispatch:** more than two commands map to their functions in a dict, not a chain
  of compare-then-return branches.
- **Fluent interfaces:** a method that *does* something returns `self`; one that has an
  answer returns the answer. Lifetimes still belong to `with`.
- **Kill dead wrappers** (a function that only forwards to one other). Question every
  `X | None` parameter, and make the value non-null higher up where you can.
- **Survey a refactor by call path**, not by category: who calls this, from where; follow
  every answer.
- **Static analysis:** leave pyright errors at zero in every file you touch. Never quote a
  baseline count as evidence that nothing is wrong.
- Copying from the owner's own projects is fine. Third-party code keeps its licence.

## Source hygiene

- **What the limit is for (owner ruling, 2026-10-16):** reducing token burn and keeping code
  readable. Never remove comments, or crush whitespace that aids readability, to get under
  it: a file over the limit gets split.
- Handwritten source: under **400 lines** is fine, 400–600 a warning, over **600** must be
  split. Split before a change crosses the line. Never raise a limit to fit a change.
- A split module becomes a package (`thing/part.py`), never prefixed siblings. Its
  `__init__` keeps the docstring and re-exports, so imports don't change.
- Shared constants and helpers are defined once (e.g. `common.py`) and imported by name.
  Each module owns its logger.
- Layers (or mixins) extend the one below; a lower layer never calls a higher one.
- A split changes no behaviour: move code verbatim, tests green before and after.
- Generated bundles are exempt, but rebuilt after source changes.

## Write it open

- Separate a function's phases (setup, work, answer) with blank lines, with a short comment
  per step where it helps. The formatter decides how much whitespace.
- A comment says what the code can't: why, which constraint or bug forced the shape, and
  what breaks if someone "simplifies" it. Delete comments that only restate the code.
- When you edit a function, open up it and its neighbours a little. Don't reformat the file.

## Tests and verification

- Use the project's venv (`.venv/bin/python`, `.venv/bin/pytest`), never system Python.
- Test everything, each test inside its scope, so a small change doesn't break everything.
- Run focused tests while iterating, the full suite just before a commit; don't commit known
  failures.
- The full suite runs in under **15 seconds**. If it slows, find out why before raising the
  budget. Use events, controlled clocks and bounded shutdown checks instead of fixed sleeps.
  Share costly stateless setup; isolate mutable state.
- For a module with a contract, write the contract tests and a mock before the real
  implementation, which must pass the same assertions. Changing a contract needs design
  discussion. Never weaken a test to let a regression through.
- Throw-away tools carry `NEVER TEST THIS` in their header and get no tests.
- Name meaningful verification gaps honestly.

## UI

- Reuse the app's shared chrome, pills/chips and confirm components. Never use the
  browser's `alert`, `confirm` or `prompt`.
- Phone layouts must stay usable, and error text must wrap.

## Reporting

- Before editing, list the files you intend to touch and why.
- After editing, list the changed files, the tests run and their results, and any notes.
  Summarise useful failures only. Don't paste whole logs.
