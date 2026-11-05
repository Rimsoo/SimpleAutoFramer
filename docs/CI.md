# CI on Forgejo Actions

Unlike GitHub, Forgejo has no hosted runners: jobs only run once a
`forgejo-runner` is registered. Until then every run stays in the *Waiting*
state with **"No matching online runner with label: …"**.

The workflow (`.forgejo/workflows/build.yml`) uses `runs-on: docker` and
starts each job in its own container (`ubuntu:24.04`, `archlinux:latest`), so
the runner machine only needs Docker. Any machine works, as long as it can
reach `https://forgejo.aaaoz.fr`: the runner connects *out* to Forgejo, so
nothing has to be exposed or port-forwarded.

## 1. Get a registration token

Either one works:

- **Instance-wide** (admin): *Site administration → Actions → Runners →
  Create new runner*.
- **Only your repos**: *User settings → Actions → Runners → Create new
  runner*.

Copy the token.

## 2a. Runner on the Unraid server (Docker)

On the Unraid console (or as a custom container in the Docker tab):

```bash
mkdir -p /mnt/user/appdata/forgejo-runner

# One-time registration (writes /data/.runner)
docker run --rm -v /mnt/user/appdata/forgejo-runner:/data -w /data \
  data.forgejo.org/forgejo/runner:13 \
  forgejo-runner register --no-interactive \
    --instance https://forgejo.aaaoz.fr \
    --token <TOKEN> \
    --name unraid \
    --labels docker:docker://node:20-bookworm

# The runner itself; it starts the job containers through the host's Docker
docker run -d --name forgejo-runner --restart unless-stopped \
  -v /mnt/user/appdata/forgejo-runner:/data -w /data \
  -v /var/run/docker.sock:/var/run/docker.sock \
  --user 0:0 \
  data.forgejo.org/forgejo/runner:13 \
  forgejo-runner daemon
```

Pick the image tag matching the current runner release listed on
<https://code.forgejo.org/forgejo/runner/releases>.

**Zoraxy / hairpin NAT**: the runner and the job containers clone the repo
through `https://forgejo.aaaoz.fr`. If that name resolves to the public IP
and the router does not support NAT loopback, clones from inside the LAN
fail. Fix it with a local DNS override pointing the domain to Zoraxy's LAN IP
(Zoraxy keeps terminating TLS, so the certificate stays valid).

## 2b. Runner on your own Arch machine

```bash
sudo pacman -S forgejo-runner docker
sudo systemctl enable --now docker
forgejo-runner register --no-interactive \
  --instance https://forgejo.aaaoz.fr --token <TOKEN> \
  --name "$(hostname)" --labels docker:docker://node:20-bookworm
forgejo-runner daemon
```

Jobs only run while this machine is on.

## 3. Check

The runner shows up as *Idle* in the runners list. Re-run the waiting job
from the repo's *Actions* tab, or push a commit.

## GitHub mirror

The GitHub workflow was removed, so the mirror no longer starts (or bills)
any GitHub Actions run.
