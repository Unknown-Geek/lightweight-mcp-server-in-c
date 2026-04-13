# Lightweight MCP Server in C

## Module 1 & 2 - Basic MCP Server Integration

### Files Added

- `vswitchd/mcp_server.c`
- `vswitchd/mcp_server.h`

### Files Modified

- `vswitchd/ovs-vswitchd.c`
  - Called `mcp_server_init()` during startup.
  - Called `mcp_server_run()` inside the main loop.
  - Called `mcp_server_close()` during shutdown.

- `vswitchd/automake.mk`
  - Added `mcp_server.c` so it gets compiled.

### Functionality

- **Server Port:** `8080`
- **Endpoint:** `POST /mcp`
- **Response:** `{"status": "ok"}`

## Module 3 - MCP Tool Routing and Hardening

### Files Modified

- `vswitchd/mcp_server.c`
  - Added HTTP request parsing and JSON dispatcher so MCP calls can route to real tool handlers.
  - Added non-blocking socket handling and `mcp_server_wait()` polling so `ovs-vswitchd` stays responsive.
  - Added `Content-Length` validation and request-size checks so malformed or incomplete requests are handled safely.
  - Switched startup and shutdown logs to OVS `VLOG` style so runtime logs are consistent with the rest of OVS.

- `vswitchd/mcp_server.h`
  - Added `mcp_server_wait()` declaration so the main loop can register MCP socket wakeups.

- `vswitchd/ovs-vswitchd.c`
  - Hooked `mcp_server_wait()` into the wait phase so incoming MCP traffic wakes the poll loop correctly.

- `vswitchd/bridge.h`
  - Added MCP bridge handler APIs so request dispatch can call bridge data collectors directly.

- `vswitchd/bridge.c`
  - Implemented handlers for `switch.get_ports`, `switch.get_flows`, and `switch.get_port_stats` so MCP returns useful switch data.

- `restart.sh`
  - Simplified to incremental build, install, and restart flow so day-to-day development is faster.

- `build.sh`
  - Kept a full bootstrap path so a clean rebuild remains one command when needed.

- `Makefile.am`
  - Added helper scripts to `EXTRA_DIST` so dist checks pass when scripts are tracked in Git.

### Functionality

- **Supported MCP Tools:** `switch.get_ports`, `switch.get_flows`, `switch.get_port_stats`
- **Request Safety:** method and path checks, JSON validation, content-length validation, max request-size guard
- **Response Style:** structured JSON success and error responses for easier debugging and integration

## Setup and Run

### Build OVS

```bash
./boot.sh
./configure
make -j4
sudo make install
```

### Start OVS

Start database:

```bash
sudo ovsdb-server \
  --remote=punix:/usr/local/var/run/openvswitch/db.sock \
  --remote=db:Open_vSwitch,Open_vSwitch,manager_options \
  --pidfile --detach
```

Initialize DB:

```bash
sudo ovs-vsctl --no-wait init
```

Start switch:

```bash
sudo ovs-vswitchd --pidfile --detach
```

### Test Endpoint

```bash
curl -X POST http://localhost:8080/mcp
```
