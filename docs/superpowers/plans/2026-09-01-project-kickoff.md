# Project Kickoff and Global Toolchain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the approved five-person design baseline into controlled collaborative development, provision this Ubuntu host with global system dependencies, and prepare an isolated `feat/user` worktree for #3 PRL.

**Architecture:** Every member develops in an independent clone and sends reviewed pull requests to `dev`; `main` remains the stable integration and demonstration branch. This host keeps `/mnt/hgfs/Desktop/SummerTermProject/project` clean on `main`, while #3 develops in `/mnt/hgfs/Desktop/SummerTermProject/worktrees/user-client`. Ubuntu dependencies are installed globally with APT; no Python virtual environment, temporary dependency prefix, or project-local toolchain is created.

**Tech Stack:** Git/GitHub, Ubuntu 25.04 APT, CMake 3.31+, Ninja 1.12+, Qt 6.8 (`Core`, `Network`, `Widgets`, `WebEngineWidgets`, `Charts`, `Test`), Python 3 system packages, Node.js 20, npm 9.

**Spec:** `docs/plans/2026-09-01-ev-charging-platform-design.md`

## Global Constraints

- Preserve every remote commit before changing branches or files; use `fetch`, fast-forward-only updates, and normal pull requests. Never force-push the shared kickoff branches.
- `main` contains only reviewed stable milestones; `dev` is the daily integration branch; module work uses `feat/web`, `feat/server`, `feat/user`, `feat/data`, and `feat/ml`.
- #2 TL owns technical decisions and merges to `dev`; #3 PRL reviews peer changes and records defects; #3's own changes require #2 review; #4 SCML owns branch/configuration records and release tags.
- This host installs dependencies globally under the Ubuntu package manager. Do not create `.venv`, `venv`, Conda, a temporary prefix, or a project-local Python/runtime installation.
- Use Ubuntu's `python3-sklearn` 1.4.2 package. Ridge regression, `TimeSeriesSplit`, MAE, and R² required by the frozen ML scope are available in that version; do not use `pip --break-system-packages` merely to reach the former 1.6 floor.
- Never commit the Tencent Map Key. Read it from `EV_TENCENT_MAP_KEY` or an ignored `config.local.ini`; do not place it in HTML, screenshots, logs, tests, fixtures, or command history embedded in documentation.
- Preserve `/mnt/hgfs/Desktop/SummerTermProject/project` as the clean core checkout. User-client implementation occurs only in the dedicated worktree.
- The deadline remains 2026-09-10, with feature freeze on 2026-09-08 and code freeze on 2026-09-09.

---

### Task 1: Publish the kickoff governance record

**Files:**
- Modify: `README.md`
- Modify: `.gitignore`
- Create: `docs/management/team-roster.md`
- Create: `docs/management/working-agreement.md`
- Create: `docs/management/daily-status.md`
- Create: `docs/review/pull-request-checklist.md`
- Modify: `docs/superpowers/plans/README.md`
- Modify: `docs/superpowers/plans/2026-09-01-platform-foundation.md`
- Modify: `docs/superpowers/plans/2026-09-01-ml-forecasting.md`

**Interfaces:**
- Consumes: the five-person role assignment already recorded in `README.md` and the branch policy in design section 11.
- Produces: one reviewed governance baseline that every module branch follows.

- [ ] **Step 1: Synchronize without overwriting teammate work**

Run from the core checkout:

```bash
git fetch origin main
git switch main
git merge --ff-only origin/main
git status --short --branch
```

Expected: `main...origin/main` with no modified or untracked files.

- [ ] **Step 2: Create the kickoff documentation branch**

```bash
if git show-ref --verify --quiet refs/heads/chore/project-kickoff; then
  git switch chore/project-kickoff
else
  git switch -c chore/project-kickoff
fi
```

Expected: the current branch is `chore/project-kickoff`, originally created from the latest `origin/main`. The branch may already contain this reviewed kickoff plan and no product code.

- [ ] **Step 3: Record the exact team roster**

Create `docs/management/team-roster.md` with this table and the kickoff date:

| ID | Name | Formal role | Development ownership | Process evidence |
|---|---|---|---|---|
| #1 | 王浩恩 | PM | Web 大屏 | 计划、风险表、答辩脚本 |
| #2 | 杨佳车 | TL | Qt 管理/服务端 | 架构、接口、技术决策 |
| #3 | 胡晟源 | PRL | Qt 用户端与腾讯地图 | 评审清单、缺陷记录、测试报告 |
| #4 | 倪宇骏 | SCML | SQLite 与模拟器 | 配置表、标签、发布清单 |
| #5 | 庞项祯 | PE | ML | 实验记录、模型指标、预测结果 |

State that the assignment became effective on 2026-09-01 and that module ownership does not override cross-module review requirements.

- [ ] **Step 4: Record the working agreement and evidence templates**

`docs/management/working-agreement.md` must state:

- each member uses an independent clone;
- `feat/* -> dev` requires a pull request;
- #3 reviews other members' pull requests, while #2 reviews #3's pull requests;
- #2 merges `dev`; #4 promotes tested `dev` milestones to `main` and creates tags;
- shared contract, Schema, root CMake, and snapshot changes require the affected owners to acknowledge the change;
- 09:00 stand-up and 18:00 integration check are recorded honestly;
- no direct SQLite access from the user client, simulator, ML publisher, or Web dashboard;
- no direct development commits on `main`.

`docs/management/daily-status.md` must contain one reusable table with columns `日期`, `成员`, `昨日完成`, `今日目标`, `阻塞`, `可运行证据`, and `状态`. `docs/review/pull-request-checklist.md` must check scope, build/test evidence, contract compatibility, secret/runtime exclusion, visible failure states, and reviewer identity.

- [ ] **Step 5: Harden ignored local artifacts and update the README status**

Keep the existing `.workbuddy/` rule and add these ignore rules:

```gitignore
build/
build-*/
cmake-build-*/
runtime/
*.db
*.db-shm
*.db-wal
config.local.ini
.env
.env.*
!.env.example
.venv/
venv/
__pycache__/
.pytest_cache/
node_modules/
dist/
*.user
*.autosave
```

Change the README state to “设计基线与人员分工已确认，项目于 2026-09-01 进入开发阶段；日常集成在 `dev`，稳定里程碑进入 `main`.” Keep the existing architecture and plan links and the full roster.

- [ ] **Step 6: Link this plan from the plan index and verify documentation**

Add `2026-09-01-project-kickoff.md` as the first operational plan in `docs/superpowers/plans/README.md`. In the foundation plan, remove `.venv` creation/activation and `requirements-dev.txt`, and replace the bootstrap package list with the exact APT list from Task 3. In the ML plan, change only the scikit-learn floor from 1.6 to 1.4 and identify the Ubuntu system package as authoritative on the demonstration host. Run:

```bash
git diff --check
rg -n "王浩恩|杨佳车|胡晟源|倪宇骏|庞项祯" README.md docs/management/team-roster.md
rg -n "main|dev|feat/user|PRL|SCML" docs/management/working-agreement.md
rg -n "EV_TENCENT_MAP_KEY|config.local.ini" .gitignore docs/management/working-agreement.md
```

Expected: no unexpected whitespace errors; all five names and the branch/secret rules are present.

- [ ] **Step 7: Commit, push, and open the kickoff pull request**

```bash
git add README.md .gitignore docs/management docs/review/pull-request-checklist.md docs/superpowers/plans/README.md docs/superpowers/plans/2026-09-01-project-kickoff.md docs/superpowers/plans/2026-09-01-platform-foundation.md docs/superpowers/plans/2026-09-01-ml-forecasting.md
git commit -m "docs: record project kickoff workflow"
git push -u origin chore/project-kickoff
gh pr create --base main --head chore/project-kickoff --title "docs: record project kickoff workflow" --body "Records the confirmed roster, branch workflow, review duties, daily evidence templates, and global-environment constraint. No product code is included."
```

Expected: one documentation-only pull request. #2 reviews the process; #4 confirms the branch and configuration rules before merge.

### Task 2: Establish the integration branch without disrupting member clones

**Files:**
- Create: `docs/management/configuration-record.md`

**Interfaces:**
- Consumes: the merged kickoff governance commit on `main`.
- Produces: remote `dev`, a short-lived `chore/foundation`, and an auditable branch baseline for all five independent clones.

- [ ] **Step 1: Wait for the kickoff pull request to merge and fast-forward the core checkout**

```bash
git switch main
git fetch origin main
git merge --ff-only origin/main
git status --short --branch
```

Expected: clean `main` at the merged kickoff commit. Do not continue while the pull request is open or changes are requested.

- [ ] **Step 2: Create and publish `dev` from the merged `main`**

```bash
git branch dev main
git push -u origin dev
```

Expected: `git ls-remote --heads origin dev` returns the same commit as `main`.

- [ ] **Step 3: Let #4 publish the five fixed module branch refs**

#4 runs this from an up-to-date independent clone so every module branch has the identical approved base:

```bash
git fetch origin
git push origin origin/dev:refs/heads/feat/web
git push origin origin/dev:refs/heads/feat/server
git push origin origin/dev:refs/heads/feat/user
git push origin origin/dev:refs/heads/feat/data
git push origin origin/dev:refs/heads/feat/ml
git push origin origin/dev:refs/heads/chore/foundation
```

The exact owner mapping is `feat/web` #1, `feat/server` #2, `feat/user` #3, `feat/data` #4, and `feat/ml` #5. #2 owns `chore/foundation`. Each owner then fetches and tracks only their assigned remote branch in their independent clone; #3 instead uses the dedicated worktree in Task 4.

- [ ] **Step 4: Record and verify the remote topology**

Create `docs/management/configuration-record.md` containing the commit ID used to create `dev`, the branch-owner table, the merge direction, and the tag ownership. Verify:

```bash
git ls-remote --heads origin main dev chore/foundation feat/web feat/server feat/user feat/data feat/ml
```

Expected: all eight shared branches are visible after their owners publish them; no branch points behind the approved kickoff commit.

### Task 3: Replace the virtual-environment bootstrap with a global APT toolchain

**Files:**
- Create later on `chore/foundation`: `scripts/check_env.sh`
- Create later on `chore/foundation`: `scripts/bootstrap.sh`
- Create later on `chore/foundation`: `docs/management/environment-matrix.md`

**Interfaces:**
- Consumes: Ubuntu 25.04 `plucky` repositories.
- Produces: globally available `/usr/bin` development commands and importable system Python modules for all project plans.

- [ ] **Step 1: Confirm the merged kickoff plans contain the global package decision**

Verify that the merged foundation plan contains this bootstrap body:

```bash
#!/usr/bin/env bash
set -euo pipefail
sudo apt-get update
sudo apt-get install -y git cmake ninja-build pkg-config nodejs npm \
  qt6-base-dev qt6-base-dev-tools qt6-webengine-dev qt6-charts-dev \
  python3-pytest python3-numpy python3-pandas python3-sklearn python3-joblib
```

Verify that the merged ML plan uses a scikit-learn floor of 1.4 and states that Ubuntu's system package is authoritative on the demonstration host. Model behavior, split rules, metrics, and golden data requirements must remain unchanged.

- [ ] **Step 2: Verify package candidates before mutation**

```bash
apt-cache policy cmake ninja-build pkg-config nodejs npm \
  qt6-base-dev qt6-base-dev-tools qt6-webengine-dev qt6-charts-dev \
  python3-pytest python3-numpy python3-pandas python3-sklearn python3-joblib
```

Expected on Ubuntu 25.04: every package has a non-empty Candidate; Qt is 6.8.x, NumPy/Pandas are 2.2.x, pytest is 8.3.x, scikit-learn is 1.4.2, and joblib is 1.4.x.

- [ ] **Step 3: Install the global toolchain**

Run the exact reviewed APT commands from Step 1 on this development host. #2 later writes the identical commands into `scripts/bootstrap.sh` on `chore/foundation`. If sudo prompts, provide the locally authorized sudo password interactively; never write it into the repository, script, shell history, logs, or documentation.

Expected: APT exits 0 without creating `.venv`, `venv`, Conda metadata, or a temporary installation prefix.

- [ ] **Step 4: Verify global command and module resolution**

```bash
command -v git cmake ninja qmake6 qtpaths6 python3 node npm
cmake --version
ninja --version
qmake6 --version
pkg-config --modversion Qt6Core Qt6Network Qt6Widgets Qt6WebEngineWidgets Qt6Charts Qt6Test
python3 -c 'import pytest, numpy, pandas, sklearn, joblib; print(pytest.__version__, numpy.__version__, pandas.__version__, sklearn.__version__, joblib.__version__)'
node --version
npm --version
```

Expected: commands resolve from system paths such as `/usr/bin`; all Qt modules report 6.8.x; all Python imports succeed using the system interpreter.

- [ ] **Step 5: Record the reproducible environment**

Give the verified output to #2 for `docs/management/environment-matrix.md` in the foundation pull request. It records the OS release, package versions, command paths, installation date, and the statement “APT global installation; no project virtual environment”. It must not include usernames, passwords, access tokens, map keys, or unrelated installed packages.

### Task 4: Prepare #3's isolated user-client worktree

**Files:**
- Create directory outside the core checkout: `/mnt/hgfs/Desktop/SummerTermProject/worktrees/user-client`

**Interfaces:**
- Consumes: published `origin/dev` and `origin/feat/user` after the foundation skeleton is merged or rebased.
- Produces: a clean local `feat/user` checkout that cannot dirty or switch the core `main` checkout.

- [ ] **Step 1: Confirm the core checkout is safe**

```bash
git -C /mnt/hgfs/Desktop/SummerTermProject/project fetch origin
git -C /mnt/hgfs/Desktop/SummerTermProject/project status --short --branch
git -C /mnt/hgfs/Desktop/SummerTermProject/project worktree list
```

Expected: the core path is clean on `main`; no existing worktree uses `feat/user` or the target directory.

- [ ] **Step 2: Add the tracked user-client worktree**

After #4 has published `feat/user`, run:

```bash
mkdir -p /mnt/hgfs/Desktop/SummerTermProject/worktrees
git -C /mnt/hgfs/Desktop/SummerTermProject/project worktree add --track -b feat/user /mnt/hgfs/Desktop/SummerTermProject/worktrees/user-client origin/feat/user
```

If the local `feat/user` branch already exists, omit `--track -b feat/user` and use `feat/user` as the final argument. Never delete or overwrite an existing target directory.

- [ ] **Step 3: Verify checkout isolation**

```bash
git -C /mnt/hgfs/Desktop/SummerTermProject/project status --short --branch
git -C /mnt/hgfs/Desktop/SummerTermProject/worktrees/user-client status --short --branch
git -C /mnt/hgfs/Desktop/SummerTermProject/project worktree list
```

Expected: the core is clean on `main`; the new worktree is clean on `feat/user`; both point to the intended current baseline.

### Task 5: Pass the formal kickoff gate and hand off to implementation plans

**Files:**
- Modify: `docs/management/daily-status.md`
- Modify: `docs/management/configuration-record.md`
- Modify: `docs/management/environment-matrix.md`

**Interfaces:**
- Consumes: Tasks 1–4, the platform foundation plan, and the Qt user-client plan.
- Produces: objective permission for the five module plans to begin without weakening the existing September 2–10 gates.

- [ ] **Step 1: Run the kickoff audit**

```bash
git -C /mnt/hgfs/Desktop/SummerTermProject/project status --short --branch
git -C /mnt/hgfs/Desktop/SummerTermProject/project ls-remote --heads origin main dev
command -v cmake ninja qmake6 python3 node npm
python3 -c 'import pytest, numpy, pandas, sklearn, joblib'
pkg-config --exists Qt6Core Qt6Network Qt6Widgets Qt6WebEngineWidgets Qt6Charts Qt6Test
```

Expected: the core main checkout is clean, remote `dev` exists, every global dependency check passes, and no virtual environment is active or required.

- [ ] **Step 2: Record the Wave 0 assignments**

The 2026-09-01 daily record must assign Foundation Tasks 1–4 to #2 with #3 review, Data Tasks 1–2 to #4, Web fixture preparation to #1, Tencent Key plus user-client readiness to #3, and ML input-contract review to #5. Each member records one runnable or reviewable artifact at the 18:00 integration check.

- [ ] **Step 3: Start the approved implementation plans at their defined boundaries**

- #2 begins `2026-09-01-platform-foundation.md` on `chore/foundation` and then `2026-09-01-admin-server.md` on `feat/server`.
- #4 begins `2026-09-01-data-simulator.md` on `feat/data`.
- #1 begins `2026-09-01-web-dashboard.md` on `feat/web` using fixtures.
- #5 begins `2026-09-01-ml-forecasting.md` on `feat/ml` using deterministic input contracts.
- #3 begins `2026-09-01-user-client.md` on the isolated `feat/user` worktree: Task 1 formatter tests and shell first, followed by a QWebEngine/Tencent minimum-load smoke test. The real Key remains local.

- [ ] **Step 4: Enforce the first two integration gates**

At 2026-09-01 18:00 require the global toolchain, five buildable/runnable entries, shared frame/envelope tests, Schema/Seed first version, and Tencent map minimum load. At 2026-09-02 18:00 require #2 and #3 to sign the exact v1 interface contract, pass login end-to-end, and allow #4 to create `v0.1-contract`. A missed gate removes P1 decoration work; it does not move the feature/code freeze dates.
