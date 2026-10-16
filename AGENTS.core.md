<!-- GENERATED from ~/Documents/AGENTS.md by ~/Documents/sync-agent-rules.
     Do not edit here: edit the master and re-run the script. -->

# AGENTS.md: shared core

These are the core rules for every coding agent in every one of the owner's projects. Each
project's own `AGENTS.md` (or `CLAUDE.md`) adds only its specifics on top, and the project
wins where the two conflict.

**Where this file lives, and how to change it:**
- The master is `~/Documents/AGENTS.md` on the owner's Mac. Each repo carries a generated,
  committed copy called `AGENTS.core.md`, so cloud and CI agents see it too.
- Claude loads the copy through the project's `CLAUDE.md` (`@AGENTS.core.md`). Codex is told
  to read it by the first line of the project's `AGENTS.md`.
- To change a core rule, edit the master and run `~/Documents/sync-agent-rules`. That
  refreshes every repo that has a copy. Give it a repo path to add a new repo. Then commit
  the copies, when the owner asks.
- Never edit `AGENTS.core.md` directly. If you are working in a clone without the master (a
  cloud session), propose the change instead.
- Copies land in public repos, so this file holds no private data.

**DRY applies to these rules too.** A rule lives in exactly one place: here if it is
general, or in the project if it is not. Never copy a core rule into a project. Point at the
section instead ("core: Push, never poll").

## How we work together

- **Be helpful, not performative.** Skip the preamble and the praise, and just do the work.
- **Have opinions.** When there is a choice, recommend one option and say why. Don't hand
  over a survey of options. Disagree when you think the owner is wrong, but say it once,
  with evidence, then do what was decided.
- **Be resourceful before asking.** Read the file, check the context, search. Come back with
  answers, not questions. Ask only for decisions that are genuinely the owner's to make.
- **Report faithfully.** If tests fail, say so and show the output. If you skipped a step,
  say so. If something is done and verified, say that plainly. Never claim a fix that
  wasn't measured or run.
- **Write in British English** (behaviour, optimise, colour) in prose, comments and docs.
- **Use absolute dates** (2026-10-16), never "yesterday" or "last week".
- **Several agents may work in the same repo.** Never revert, reformat or "tidy" changes you
  didn't make and don't understand. Ask instead.
- **Context is a budget.** Start a new task in a fresh session. Compact, or suggest
  compacting, often. Design and implement at the same model and thinking level.

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
- Planned but unscheduled work goes in `BACKLOG.md`. Add ideas there and don't start them
  unless asked.
- When the owner makes a decision or corrects you, record it in the right rules file,
  dated, as a rule plus its reason ("Why: in 2026.10.2 …"). The reason is what stops the
  next agent from arguing it away. Put general lessons here and project lessons in the
  project's file.

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
  docs or commit messages. That covers names, phone numbers, addresses, IPs, house, room
  and device names, tokens and serials. A real example the owner typed in chat is still real.
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
  never a placeholder. If more than one agent contributed, stack the trailers.
- A refactor and a functional fix go in separate commits.
- Never commit to `main` directly when the project uses a `dev` branch.
- Bump the version of whatever you change, the way the project file says. Most changes bump
  the build number, and larger ones bump the minor. Only the owner bumps the major.

## Exploring code (tokensave)

Where `.tokensave/` exists, tokensave is the rule, not a suggestion:

- Understand code with `tokensave_context`. Find a symbol with `tokensave_search`, or with
  `literal: true` for call sites. Use `tokensave_callers`, `_callees` and `_impact` for
  connections and `tokensave_files` for layout. Don't use `grep`, `rg`, `find`, `ls -R` or
  `cat`.
- Read a file you won't edit with `tokensave_read` (`mode: "map"` or `"lines"`). Use the
  harness's own reader only on a file you are about to edit.
- The tools may be deferred. Load them with `ToolSearch` rather than skipping them because
  they "aren't available".
- Call `tokensave_status` first. If it shows a branch fallback, run
  `tokensave branch add <name>` or stop. After editing, run `tokensave sync` before querying
  again. Report new files, because the index won't know about them yet.
- The index is disposable and gitignored. `rm -rf .tokensave && tokensave init` rebuilds it,
  then reconnect the MCP server (`/mcp`). Other indexed projects can be queried read-only
  with `graph_root`.
- Never launch an Explore (or any) agent for code research.
- Bash is fine for tests, linters, git and non-code files.

## Nothing fails silently

A fault nobody knows about can't be fixed, and silence about a fault is the worst bug.

- Show failures to the user, with enough detail to investigate. Never turn a failure into
  apparent success.
- Never hide a startup or background-thread exception behind a generic "not ready". Keep
  the original exception as the cause, log it, and test the failure path.
- **Logging:** use `logging.getLogger(__name__)` (or the language's equivalent), never
  `print`. The log is how problems get diagnosed, often remotely.
  - INFO for start/stop, config changes, actions taken and events in or out.
  - DEBUG for raw traffic and polls.
  - WARNING/ERROR for failures, refusals and retries, with the reason.
  - Never log secrets, or request bodies that could carry them.
- **Safe by construction.** "Connected", "live" or "current" is a lease that lapses by
  itself, not a flag that code must remember to clear. Only fresh proof renews it, and on
  lapse it shows its age ("not current since …"). A copy of the other side's state is the
  same kind of claim. Every hop of an update path needs a heartbeat, a deadline and a
  visible age.
- Tie cleanup to a construct the language must run, such as `with`, `try/finally`, lifecycle
  hooks or an abort with a deadline. Never rely on having foreseen every failure path.
- Shutdown is prompt: signal workers first, then await their cancellation and cleanup. Never
  abandon tasks.

## Push, never poll

State changes arrive by events or push. A new poll, a shorter interval, or "it'll update at
the next refresh" needs the owner's explicit approval every time: ask, don't decide.
Existing approved health checks stay as they are. Time-based work (a date coming due) is
not a poll.

## Code rules

- **DRY.** Never repeat. A repeated *pattern* counts too, even when the code isn't
  identical: give it one shared implementation (a base class, a generalisation, a table, or
  generated code).
- **YAGNI.** Don't build things because you *might* need them.
- **No slop.** No filler code, no boilerplate "for later", and no comments that restate the
  code. Prefer the simplest thing that works (Ponytail).
- **Optimise: do it once.** Don't do it if you don't have to, and if you have to, do it only
  once. Data derived from immutable inputs is computed once and persisted. Recomputing it
  per request, or doing an O(n) pass per event, is a defect, even behind a cache.
- **Measure** before diagnosing and after fixing. "It feels faster" is not a result. Check
  that an edit actually landed before believing any number from it. Never trade correctness
  for speed quietly.
- **Existing solutions first.** Reuse helpers already in this repo, then the standard
  library, then native platform features, then a maintained open-source library. Build
  custom only when those won't do. Don't recommend paid services without the owner's
  approval. Make the smallest complete change.
- **Git is the merge engine.** Never hand-build version comparison, digests, merge bases or
  three-way merges over files that live in git. Give the two sides shared ancestry and merge
  them.
- **Command dispatch:** when there are more than two commands, use a dict mapping each
  command to its function, not a chain of compare-then-return branches.
- **Fluent interfaces:** a method that *does* something returns `self`, so calls chain. A
  method that has an answer returns the answer. Lifetimes still belong to `with`.
- **Kill dead wrappers**, meaning a function that only forwards to one other. Question every
  `X | None` parameter, and make the value non-null higher up where you can.
- **Survey a refactor by call path**, not by category. Ask who calls this and from where, and
  follow every answer.
- **Static analysis:** leave pyright errors at zero in every file you touch. Never quote a
  baseline count as evidence that nothing is wrong.
- Copying from the owner's own projects is fine. Third-party code keeps its licence.

## Source hygiene

- **What the limit is for (owner ruling, 2026-10-16):** the size limit exists to reduce
  token burn and keep code readable. Massive files are hard for everyone, human or agent, to
  reason about, and that is when the split trigger applies. You may never remove comments,
  or crush whitespace that aids readability, to get under an arbitrary number. A file over
  the limit gets split.
- Handwritten source: under **400 lines** is fine, 400–600 is a warning, and over **600**
  must be split. Split before a change crosses the line. Never raise a limit to fit a change.
- A split module becomes a package/folder (`thing/part.py`), never prefixed siblings. Its
  `__init__` keeps the docstring and re-exports, so imports don't change.
- Shared constants and helpers are defined once (for example `common.py`) and imported by
  name. Each module owns its logger.
- Layers (or mixins) extend the one below, and a lower layer never calls a higher one.
- A split changes no behaviour. Move code verbatim, with tests green before and after.
- Generated bundles are exempt from the limits, but rebuild them after source changes.

## Write it open

- Separate a function's phases (setup, work, answer) with blank lines, with a short comment
  per step where it helps. `ruff format` decides how much whitespace.
- A comment says what the code can't: why, which constraint or bug forced the shape, and
  what breaks if someone "simplifies" it. Delete comments that only restate the code.
- When you edit a function, open up it and its neighbours a little. Don't reformat the file.

## Tests and verification

- Use the project's venv (`.venv/bin/python`, `.venv/bin/pytest`), never system Python.
- Test everything, but keep each test inside its scope, so that a small change doesn't break
  everything.
- Run focused tests while iterating. Run the full suite only just before a commit, and don't
  commit known failures.
- The full suite runs in under **15 seconds**. If it slows down, find out why before raising
  the budget. Use events, controlled clocks and bounded shutdown checks instead of fixed
  sleeps. Share costly stateless setup, but isolate mutable state.
- For a module with a contract, write the contract tests and a mock before the real
  implementation, and the real one must pass the same assertions. Changing a contract needs
  design discussion. Never weaken a test to let a regression through.
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
