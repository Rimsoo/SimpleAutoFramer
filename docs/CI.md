# CI on Forgejo Actions

Unlike GitHub, Forgejo has no hosted runners: jobs only run once a
`forgejo-runner` is registered. Until then every run stays in the *Waiting*
state with **"No matching online runner with label: …"**.

The workflow (`.forgejo/workflows/build.yml`) uses `runs-on: docker` and
starts each job in its own container (`ubuntu:24.04`, `archlinux:latest`), so
the runner machine only needs Docker. Any machine works, as long as it can
reach `https://forgejo.aaaoz.fr`: the runner connects *out* to Forgejo, so
nothing has to be exposed or port-forwarded.

## 1. Create the runner in Forgejo

*User settings → Actions → Runners → Create new runner* (or *Site
administration → Actions → Runners* for an instance-wide runner). Recent
Forgejo versions create the runner right away and show its **UUID** and
**token** once; the old `forgejo-runner register` + registration token flow
fails with *"runner registration token not found"*.

Prefer a user-level runner: with the Docker socket mounted, a workflow can
take control of the host, so only run trusted code on it.

## 2a. Runner on the Unraid server

Install **ForgejoRunner** from Community Apps (Docker-socket mode), but
before clicking *Apply* create its config from the Unraid terminal:

```bash
mkdir -p /mnt/user/appdata/forgejo-runner/data
cd /mnt/user/appdata/forgejo-runner/data
docker run --rm --entrypoint forgejo-runner data.forgejo.org/forgejo/runner:12 \
  generate-config > runner-config.yml
tail -2 runner-config.yml   # must end with "  connections:"
cat >> runner-config.yml <<'YAML'
    forgejo:
      url: https://forgejo.aaaoz.fr/
      uuid: <UUID>
      token: <TOKEN>
      labels:
        - docker:docker://node:20-bookworm
YAML
chown -R 1000:1000 . && chmod 600 runner-config.yml
```

Then *Apply*, and check the container logs.

Without the template, the same config works with a plain container:

```bash
docker run -d --name forgejo-runner --restart unless-stopped \
  -v /mnt/user/appdata/forgejo-runner/data:/data -w /data \
  -v /var/run/docker.sock:/var/run/docker.sock --user 0:0 \
  data.forgejo.org/forgejo/runner:12 \
  forgejo-runner daemon --config /data/runner-config.yml
```

**Zoraxy / hairpin NAT**: the runner and the job containers clone the repo
through `https://forgejo.aaaoz.fr`. If that name resolves to the public IP
and the router does not support NAT loopback, clones from inside the LAN
fail. Fix it with a local DNS override pointing the domain to Zoraxy's LAN IP
(Zoraxy keeps terminating TLS, so the certificate stays valid).

## 2b. Runner on your own Arch machine

```bash
sudo pacman -S forgejo-runner docker
sudo systemctl enable --now docker
forgejo-runner generate-config > runner-config.yml
# append the same "forgejo:" connection block as above, then:
forgejo-runner daemon --config runner-config.yml
```

Jobs only run while this machine is on.

## 3. Check

The runner shows up as *Idle* in the runners list. Re-run the waiting job
from the repo's *Actions* tab, or push a commit.

## GitHub mirror

The GitHub workflow was removed, so the mirror no longer starts (or bills)
any GitHub Actions run.
