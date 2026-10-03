#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="/home/soda/napotim-esp-monitor"
mkdir -p "$ROOT/.secrets/bin"
cat > "$ROOT/.secrets/bin/docker" <<'WRAPPER'
#!/usr/bin/env bash
exec "/mnt/c/Program Files/Docker/Docker/resources/bin/docker.exe" "$@"
WRAPPER
chmod 700 "$ROOT/.secrets/bin/docker"
export PATH="$ROOT/.secrets/bin:$PATH"
export DATABASE_URL="postgresql+asyncpg://cube:cube-test@172.18.128.1:55433/napotim_esp_test"
export TEST_DATABASE_URL="$DATABASE_URL"
cd /home/soda/todo-ai-pwa
./scripts/dev-check.sh
