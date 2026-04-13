#!/usr/bin/env bash
set -euo pipefail

# Run this from the OVS repo root.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# Install location defaults
PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"
SBINDIR="$PREFIX/sbin"

# Runtime paths
ETC_DIR="$PREFIX/etc/openvswitch"
VAR_DIR="$PREFIX/var"
RUN_DIR="$VAR_DIR/run/openvswitch"
LOG_DIR="$VAR_DIR/log/openvswitch"
DB_FILE="$ETC_DIR/conf.db"
DB_SOCK="$RUN_DIR/db.sock"
OVSDB_PIDFILE="$RUN_DIR/ovsdb-server.pid"
VSWITCHD_PIDFILE="$RUN_DIR/ovs-vswitchd.pid"

echo "[1/3] Build incremental changes..."
make -j"$(nproc)"

echo "[2/3] Install..."
sudo make install

echo "[3/3] Restart OVS daemons..."
sudo mkdir -p "$RUN_DIR" "$LOG_DIR"

if [ -f "$VSWITCHD_PIDFILE" ]; then
  sudo "$BINDIR/ovs-appctl" -t "$VSWITCHD_PIDFILE" exit || true
fi
if [ -f "$OVSDB_PIDFILE" ]; then
  sudo "$BINDIR/ovs-appctl" -t "$OVSDB_PIDFILE" exit || true
fi

sudo pkill -x ovs-vswitchd || true
sudo pkill -x ovsdb-server || true
sudo rm -f "$DB_SOCK" "$OVSDB_PIDFILE" "$VSWITCHD_PIDFILE"

if [ ! -f "$DB_FILE" ]; then
  echo "Missing DB file: $DB_FILE"
  echo "Create it once with:"
  echo "sudo $BINDIR/ovsdb-tool create $DB_FILE $ROOT_DIR/vswitchd/vswitch.ovsschema"
  exit 1
fi

sudo "$SBINDIR/ovsdb-server" \
  --remote="punix:$DB_SOCK" \
  --remote=db:Open_vSwitch,Open_vSwitch,manager_options \
  --pidfile="$OVSDB_PIDFILE" \
  --detach \
  --log-file="$LOG_DIR/ovsdb-server.log" \
  "$DB_FILE"

sudo "$BINDIR/ovs-vsctl" --db="unix:$DB_SOCK" --no-wait init

sudo "$SBINDIR/ovs-vswitchd" "unix:$DB_SOCK" \
  --pidfile="$VSWITCHD_PIDFILE" \
  --detach \
  --log-file="$LOG_DIR/ovs-vswitchd.log"

echo ""
echo "OVS restart complete."