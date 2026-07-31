# CI.1 Windows x64 Release verification

Status: **CI.1 implemented and awaiting manual workflow/artifact acceptance.**

## Purpose

CI.1 moves the authoritative complete deterministic test run and final Windows
x64 Release compilation to a clean GitHub-hosted `windows-2022` runner. The
runner produces a downloadable, hash-verified application artifact. Local work
remains available for editing, review, static checks, focused diagnostics,
artifact installation and native manual validation.

This boundary was introduced after two freezes of the user's Windows music PC
during heavy JUCE compilation. The collected logs did not prove a hardware
fault, and LTCG had remained disabled. Moving the authoritative clean build
reduces repeated heavy compilation on that machine without inventing a cause
for the freezes.

CI.1 changes build and distribution infrastructure only. It does not change
DAWHermes musical behaviour, project persistence, VST3 hosting semantics or any
other product feature.

## Hosted runner boundary

A GitHub-hosted runner is a temporary clean VM supplied by GitHub and discarded
after the run. DAWHermes uses `windows-2022` with the Visual Studio 2022 x64
toolchain and official Python setup for CPython 3.11 x64. The workflow has
read-only repository permission and uses no secrets.

A self-hosted runner would execute the same heavy workload on the user's PC and
would therefore not remove the local-freeze risk. The user's computer must
never be registered as a DAWHermes self-hosted runner.

The workflow configures one fresh `build-ci` tree containing Release only. It
uses one CMake/MSBuild/compiler worker, keeps `DAWHERMES_ENABLE_LTCG=OFF`, runs
the complete deterministic suite first, and then incrementally builds the
application in that same tree. It verifies:

- every final DAWHermes target has `/GL-` and normal Release optimization;
- the final executable linker has `/LTCG:OFF` and no active `/LTCG`;
- the runtime executable has an AMD64/x64 PE header;
- every packaged runtime file has a matching SHA-256 manifest entry.

The environment-dependent embedded Hermes integration is reported as skipped.
Hosted CI does not require physical audio hardware, ASIO, commercial VST3
instruments, VST3 scanning or licence systems, user MIDI/WAV assets, ACE Studio,
Composer Assistant, Cubase, Reaper or other network services. Private endpoints,
licences, real music assets, plug-in catalogs and local settings must never be
sent to the runner.

Build-output and dependency-source caches are intentionally deferred until this
clean baseline has demonstrated repeatable green builds.

## Workflow and diagnostics

The workflow is [Windows x64 Release](../.github/workflows/windows-x64-release.yml).
It runs for build-relevant pushes to `main` and `wip/**`, build-relevant pull
requests targeting `main`, and manual dispatch of a selected branch. Pure
documentation acceptance commits are excluded by path filtering.

Codex checks an exact pushed commit rather than relying on an older successful
run. Typical authenticated inspection commands are:

```powershell
gh run list --workflow "Windows x64 Release" --branch <BRANCH>
gh run watch <RUN_ID> --exit-status
gh run view <RUN_ID>
gh run view <RUN_ID> --log-failed
```

Each run uploads a separate diagnostic logs artifact even when a phase fails.
The logs contain sanitized tool versions and phase transcripts, flag/PE/package
reports and the job summary. They do not contain the runtime package, complete
environment dumps, credentials or user/private configuration.

## Runtime artifact

A successful run uploads an artifact named:

```text
DAWHermes-Windows-x64-Release-<COMMIT_SHA>
```

Its extracted contents are:

```text
DAWHermes-Windows-x64-Release/
|-- app/
|   |-- DAWHermes.exe
|   `-- required runtime files, when present
|-- Install-DAWHermes.ps1
|-- BUILD-INFO.txt
`-- SHA256SUMS.txt
```

`BUILD-INFO.txt` identifies the exact commit, ref, workflow run, UTC build time,
toolchain and pinned dependency versions, Release/x64 configuration, LTCG state
and truthful test status. It contains no absolute workspace path.

`SHA256SUMS.txt` covers every file under `app/` with a deterministic relative
path. The installer rejects malformed entries, traversal, missing or unlisted
files, hash mismatches and non-x64 executables before installation.

Artifacts are retained for 14 days. They are not GitHub Releases and are not
code-signed in CI.1; Windows may therefore show the normal warning for an
unsigned downloaded application.

## Download and install without compiling

Open the successful Actions run in GitHub, select its runtime artifact and
extract the downloaded archive to an ignored local directory. Authenticated CLI
download is also supported:

```powershell
gh run download <RUN_ID> `
    --name DAWHermes-Windows-x64-Release-<COMMIT_SHA> `
    --dir ci-downloads\<COMMIT_SHA>
```

From the extracted artifact, run:

```powershell
.\Install-DAWHermes.ps1
```

The script verifies `BUILD-INFO.txt`, all hashes and the x64 PE header, refuses
to proceed while DAWHermes is running, and copies only the already compiled
runtime to `%LOCALAPPDATA%\DAWHermes\app`. It creates or updates direct Desktop
and Start Menu shortcuts. It does not configure CMake, compile, link, install
Python or plug-ins, change `PATH`, require administrator rights, or overwrite
settings and catalogs stored outside the application directory.

## Manual validation remains required

The hosted runner never launches the GUI and cannot validate the real audio
device or a locally licensed VST3. After downloading and installing the exact
green artifact, Codex performs only a bounded native smoke check. The project
owner remains responsible for manual native validation of the installed
application, including the targeted VST3/WAV/MIDI workflow. A green artifact is
not manual acceptance and does not authorize a PR or merge.

Project save/load and cross-session plug-in-state persistence remain
unavailable. CI.1 makes no project-persistence change.
