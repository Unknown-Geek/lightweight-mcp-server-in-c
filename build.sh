#!/usr/bin/env bash
set -euo pipefail

# Run from anywhere; script will switch to repo root.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"
SBINDIR="$PREFIX/sbin"

ETC_DIR="$PREFIX/etc/openvswitch"
VAR_DIR="$PREFIX/var"
RUN_DIR="$VAR_DIR/run/openvswitch"
LOG_DIR="$VAR_DIR/log/openvswitch"

DB_FILE="$ETC_DIR/conf.db"
SCHEMA_FILE="$ROOT_DIR/vswitchd/vswitch.ovsschema"
DB_SOCK="$RUN_DIR/db.sock"
OVSDB_PIDFILE="$RUN_DIR/ovsdb-server.pid"
VSWITCHD_PIDFILE="$RUN_DIR/ovs-vswitchd.pid"

echo "[1/8] Stop old daemons (if running)..."
if [ -f "$VSWITCHD_PIDFILE" ]; then
  sudo "$BINDIR/ovs-appctl" -t "$VSWITCHD_PIDFILE" exit || true
fi
if [ -f "$OVSDB_PIDFILE" ]; then
  sudo "$BINDIR/ovs-appctl" -t "$OVSDB_PIDFILE" exit || true
fi
sudo pkill -x ovs-vswitchd || true
sudo pkill -x ovsdb-server || true
sudo rm -f "$DB_SOCK" "$OVSDB_PIDFILE" "$VSWITCHD_PIDFILE"

echo "[2/8] Clean previous build artifacts..."
if [ -f Makefile ]; then
  make distclean || true
fi

echo "[3/8] Regenerate configure files..."
./boot.sh

echo "[4/8] Configure..."
./configure --prefix="$PREFIX" --sysconfdir="$PREFIX/etc" --localstatedir="$PREFIX/var"

echo "[5/8] Build..."
make -j"$(nproc)"

echo "[6/8] Install..."
sudo make install

echo "[7/8] Prepare DB and runtime directories..."
sudo mkdir -p "$ETC_DIR" "$RUN_DIR" "$LOG_DIR"
if [ ! -f "$DB_FILE" ]; then
  sudo "$BINDIR/ovsdb-tool" create "$DB_FILE" "$SCHEMA_FILE"
fi

echo "[8/8] Start ovsdb-server and ovs-vswitchd..."
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
echo "Build + restart complete."