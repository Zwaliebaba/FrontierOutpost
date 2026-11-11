# Frontier Outpost on Azure

One small Linux VM runs two containers:

- **Caddy** serves the page at `https://outpost.zwaliebaba.com` and gets and renews its certificate
  from Let's Encrypt.
- **The game server** reads that same certificate for WebTransport. Browsers reach WebTransport
  directly on UDP 4443.

Saves live on the VM's disk.

Why a VM: Container Apps and App Service accept no inbound UDP, which WebTransport needs. Container
Instances has no local disk, and SQLite must not live on an SMB share.

The image is built on your own machine: the VM's 1 GB of memory is too little to compile the
server.

## What it costs

A `Standard_B2ats_v2` (2 burstable vCPU, 1 GB), a 30 GB Standard SSD and a static public IPv4
address come to roughly €12–15 a month in West Europe; check the Azure pricing calculator. A
deallocated VM costs only its disk and address, so let it shut down when no one is playing
(step 2).

## 1. Build and push the image (your machine)

GitHub's container registry needs a **classic** personal access token with `write:packages`. Make
one under GitHub → Settings → Developer settings, then:

```bash
docker login ghcr.io -u zwaliebaba
```

From the repository root, build and push, tagged by date or commit:

```bash
docker build -t ghcr.io/zwaliebaba/frontier-outpost:2026-11-12 .
```

```bash
docker push ghcr.io/zwaliebaba/frontier-outpost:2026-11-12
```

Docker Desktop on an x64 Windows machine builds `linux/amd64`, which the VM size below runs.

## 2. Create the VM (Azure CLI)

```bash
az group create -n frontier -l westeurope
```

```bash
az vm create -g frontier -n frontier --image Ubuntu2404 --size Standard_B2ats_v2 \
  --admin-username azureuser --ssh-key-values ~/.ssh/id_ed25519.pub \
  --public-ip-sku Standard --os-disk-size-gb 30 --storage-sku StandardSSD_LRS
```

Open the web ports to everyone, and WebTransport's UDP port. Let's Encrypt needs port 80 too:

```bash
az network nsg rule create -g frontier --nsg-name frontierNSG -n web --priority 1010 \
  --protocol Tcp --destination-port-ranges 80 443 --access Allow
```

```bash
az network nsg rule create -g frontier --nsg-name frontierNSG -n webtransport --priority 1020 \
  --protocol Udp --destination-port-ranges 4443 --access Allow
```

Keep SSH to your own address:

```bash
az network nsg rule update -g frontier --nsg-name frontierNSG -n default-allow-ssh \
  --source-address-prefixes <your-ip>/32
```

Optionally, deallocate it every night at 23:00 UTC; `az vm start -g frontier -n frontier` brings it
back:

```bash
az vm auto-shutdown -g frontier -n frontier --time 2300
```

Note the public address for the next step:

```bash
az vm show -d -g frontier -n frontier --query publicIps -o tsv
```

## 3. Point the name at it (Cloudflare)

In Cloudflare's DNS for `zwaliebaba.com`, add an **A** record `outpost` → that address with the
proxy status set to **DNS only** (the grey cloud).

The orange cloud would break the game: Cloudflare's proxy cannot carry WebTransport, and it would
answer Let's Encrypt's check itself.

## 4. Run it on the VM

SSH in (`ssh azureuser@<address>`) and install Docker:

```bash
sudo apt-get update && sudo apt-get install -y docker.io docker-compose-v2
```

```bash
sudo usermod -aG docker $USER && exit
```

SSH in again. Make the folders, owned by the game server's user (uid 10001): Caddy runs as that
user too, so the server can read the certificate's key.

```bash
sudo mkdir -p /srv/frontier/caddy/data /srv/frontier/caddy/config /srv/frontier/saves
```

```bash
sudo chown -R 10001:10001 /srv/frontier
```

Copy this folder's `compose.yaml` and `Caddyfile` to `~/frontier` on the VM (for example with
`scp deploy/azure/compose.yaml deploy/azure/Caddyfile azureuser@<address>:frontier/`). Then, in
`~/frontier`, write the `.env` file the two read:

```bash
printf 'FRONTIER_DOMAIN=outpost.zwaliebaba.com\nFRONTIER_IMAGE=ghcr.io/zwaliebaba/frontier-outpost:2026-11-12\n' > .env
```

Log in to the registry with a classic token that only has `read:packages`, then start both:

```bash
docker login ghcr.io -u zwaliebaba
```

```bash
docker compose up -d
```

```bash
docker compose logs -f
```

Within a minute Caddy logs `certificate obtained successfully`. The server, which logged
`waiting for the certificate`, then logs `open https://outpost.zwaliebaba.com`. Open that address in
Chrome or Edge.

Passkeys bind to the name, so every pilot registers anew there; local saves do not move.

## Updating

Build and push a new tag (step 1), change `FRONTIER_IMAGE` in `.env`, then:

```bash
docker compose pull && docker compose up -d
```

Runs in progress survive the restart from their checkpoints.

## Server commands

Run these in `~/frontier` on the VM.

```bash
docker compose exec server frontier-server grant <callsign> 1000cr,40ore,20alloy,10components
```

```bash
docker compose exec server frontier-server report
```

`wipe` needs the server stopped:

```bash
docker compose stop server && docker compose run --rm server frontier-server wipe && docker compose start server
```

To give every new pilot extra stock, add `FRONTIER_GRANT=1000cr,40ore` to `.env` and run
`docker compose up -d`.

## When it does not work

- **Caddy gets no certificate.** The `outpost` record must point at the VM and be DNS only. Port 80
  must be open (step 2). Let's Encrypt limits failed attempts, so fix the cause before restarting
  Caddy again and again.
- **The server keeps waiting for the certificate.** It gives up after 10 minutes and Docker
  restarts it. `ls -l /srv/frontier/caddy/data/caddy/certificates/` shows what Caddy has. Files that
  belong to root mean step 4's `chown` was missed.
- **The page loads but stays at "Connecting".** UDP 4443 is not open (step 2), or the browser is not
  Chromium-based (decided in AGENTS.md §8).
- **Renewal.** Caddy renews about a month before expiry. The server checks the files hourly and
  presents the new certificate to new connections without a restart.

## Removing it

```bash
az group delete -n frontier
```

Delete the DNS record too.

## Open to the internet

Anyone who finds the name can register: there is no invitation or rate limit. For a closed
playtest, change the `web` rule's `--source-address-prefixes` to the testers' addresses.
