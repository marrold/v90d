# ARM CI on a throwaway Mythic Beasts Pi 4 — setup

The idea: GitHub's own cloud runner does the build + x86 tests; for the ARM
suite it provisions a Mythic Beasts Pi 4 on demand, runs the tests on it
natively (no qemu — the A72 runs the 32-bit ARMv5 binaries at EL0), then
deletes it. Nothing is left running; idle cost is zero. None of this lives on
the branches you send upstream.

## 1. Repo layout (keeps `main` a clean mirror of upstream)

| branch      | role                         | default? |
|-------------|------------------------------|----------|
| `ci`        | holds `.github/` + this file | yes      |
| `main`      | tracks upstream `main`       | no       |
| `feature/*` | work, **cut from `main`**    | no       |

- Set `ci` as the **default branch** (Settings → Branches). `workflow_dispatch`
  only registers on the default branch — that is the one requirement.
- Always branch features off `main` (`git checkout -b feature/x main`) so their
  diff vs upstream carries no CI files.
- Put `.github/workflows/arm-tests.yml` (from this folder) on the `ci` branch.

Fork vs standalone mirror: a GitHub *fork* works, but GitHub disables *scheduled*
workflows on forks — that's why the reaper is folded into `arm-tests.yml` as a
step instead of a cron job. If you ever want a cron reaper, use a standalone
public mirror repo (not the fork button) where `on: schedule` is allowed.

## 2. Mythic Beasts API key  (NOT your account login)

1. Log into the control panel with your normal account.
2. Go to **API users** (`/customer/api-users`) → create a key.
3. **Scope it to the Raspberry Pi API only** — if it leaks via CI it can then
   only create/delete Pis, not touch billing/DNS/account.
4. Copy the **Key ID** and the **secret** (the secret is shown once).

## 3. Dedicated CI SSH key

```sh
ssh-keygen -t ed25519 -f ci_pi -N ''     # ci_pi (private), ci_pi.pub (public)
```
The workflow injects the public half at provision time; you only store the
private half as a secret.

## 4. GitHub secrets & variables  (Settings → Secrets and variables → Actions)

Secrets:
- `MB_KEY_ID`  — the API Key ID
- `MB_SECRET`  — the API secret
- `MB_SSH_KEY` — contents of the **private** `ci_pi` file

Optional variable:
- `MB_OS_IMAGE` — override the OS image (default `rpi-bookworm-arm64`,
  = Raspberry Pi OS Bookworm 12, 64-bit).

Because the workflow is `workflow_dispatch`-only, these secrets are never
exposed to fork-PR-triggered runs and only you (write access) can start a run.

## 5. Running it

```sh
gh workflow run arm-tests.yml -f test_ref=feature/x        # from the ci branch
```
Or add this to your shell profile to push + test the current branch in one go:
```sh
armcheck() {
  git push origin HEAD &&
  gh workflow run arm-tests.yml -R OWNER/REPO -f test_ref="$(git branch --show-current)"
}
```
Flow: `x86` runs immediately on GitHub's cloud; if it passes, `arm` provisions a
Pi (~2-3 min), SSHes in, runs `make check-arm QEMU= ARM_CC=arm-linux-gnueabi-gcc`
natively, and deletes the Pi (even on failure/cancel). The first step of `arm`
also reaps any orphaned `ci-*` Pi older than 2 h.

## 6. Gate merges (optional, in your own repo)

Open an internal PR `feature/x → main`, comment-trigger or dispatch the run, and
have it post a commit status (add a status-post step keyed on the PR head SHA).
Then add a branch-protection rule on `main` requiring that status. Merges block
until ARM is green. (Upstream has its own merge rules; this is just your gate.)

## 7. Send it upstream

```sh
gh pr create --repo UPSTREAM-OWNER/v90d --base main --head OWNER:feature/x
```
Because `feature/x` was cut from the clean `main` mirror, the diff is only your
code — no `.github`, no CI.

## Known limitation

This runs the root `make check-arm` (libv90modem + `ppp cfg tel dce srv arch`),
which honours `ARM_CC`, so apt's `gcc-arm-linux-gnueabi` is enough. It does NOT
run `fw/`'s own `test-v90nv` ARM path: that uses the pinned `$(CROSS)` toolchain
(for kernel/loader/v90nv.arm) which isn't installed on the throwaway Pi. Those
stay with the release build (x86 + pinned toolchain). If you want them here too,
add a step that fetches the pinned toolchain onto the Pi and runs
`make -C fw test-v90nv QEMU=`.
