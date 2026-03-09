# Empire Multi-User Architecture

This document describes the three-tier architecture implemented for multi-user Empire gameplay, designed to mirror historical multi-user Empire servers like Peter Langston's Wolfpack Empire.

## Overview

The architecture separates concerns into three distinct layers:

1. **Telnet Frontend** (empire_frontend) - Network-facing component
2. **Authoritative Server** (empire_server) - Game state daemon
3. **Game Client** (empire_client) - Per-session terminal handler

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                      Client Connections                     │
│                    (telnet yourhost 5000)                   │
└────────────────────┬────────────────────────────────────────┘
                     │
              ┌──────▼──────┐
              │  Frontend   │  Port 5000 (Public TCP)
              │  (Telnet)   │  - Rate limiting
              │             │  - PTY allocation
              └──────┬──────┘  - Security controls
                     │
              ┌──────▼──────┐
              │    PTY      │  Per-session pseudo-terminal
              └──────┬──────┘
                     │
              ┌──────▼──────┐
              │   Client    │  Per-session process
              │ (Terminal)  │  - UI handling
              └──────┬──────┘  - Display rendering
                     │
              ┌──────▼──────┐
              │   Server    │  Port 4000 / Unix Socket
              │  (Game)     │  - Authoritative state
              └──────┬──────┘  - Atomic persistence
                     │
              ┌──────▼──────┐
              │   World     │  /var/lib/empire/world.dat
              │  Storage    │  - Atomic writes
              └─────────────┘  - File locking
```

## Components

### 1. empire_frontend

The network-facing component that handles all external connections.

**Responsibilities:**
- Listen on public TCP port (default: 5000)
- Telnet RFC 854 negotiation
- Rate limiting per IP address
- PTY allocation per connection
- Process isolation (fork/exec)
- Security controls (privilege dropping, chroot)

**Configuration Options:**
```bash
./empire_frontend -d -p 5000 -s /var/run/empire/server.sock
  -d: Run as daemon
  -p: Listen port
  -s: Backend Unix socket path
  -u: Run as user after binding
```

**Security Features:**
- Per-IP rate limiting (100 connections per 60 seconds)
- Privilege dropping after socket binding
- Environment sanitization
- TCP keepalive
- Connection timeouts (30 minutes idle)

### 2. empire_server

The authoritative game server that maintains world state.

**Responsibilities:**
- Listen on localhost/unix socket (default: 4000)
- Maintain game state in memory
- Atomic world persistence
- File locking for concurrent access
- Mutation logging
- Multi-player session management

**Configuration Options:**
```bash
./empire_server -d -s /var/run/empire/server.sock -u empire
  -d: Run as daemon
  -s: Unix socket path (alternative to TCP)
  -p: TCP port (default: 4000)
  -u: Run as user after initialization
```

**Data Storage:**
- World state: `/var/lib/empire/world.dat`
- Log files: `/var/log/empire/server.log`
- Lock file: `/var/lib/empire/world.dat.lock`
- PID file: `/var/run/empire/server.pid`

**Atomic Writes:**
The server uses rename-overwrite pattern for atomic saves:
1. Write to temporary file (`world.dat.tmp`)
2. fsync temporary file
3. Atomic rename to `world.dat`
4. File locking prevents concurrent writes

### 3. empire_client

The terminal client that runs inside each PTY session.

**Responsibilities:**
- Terminal handling (raw mode)
- User interface
- Command processing
- Display rendering
- Server communication

**Protocol:**
- Text-based protocol over TCP/Unix socket
- Commands: L=Login, M=Move, A=Attack, B=Build, F=Function, E=End Turn, Q=Quit
- Automatic reconnection on disconnect

## Protocol

### Client-Server Protocol

The protocol is line-based with single-character command codes:

**Client to Server:**
```
Lusername:nation    # Login
Munit direction     # Move unit
Aloc target         # Attack location
Bcity unit          # Build unit at city
Funit func          # Set unit function
E                   # End turn
Q                   # Quit
P                   # Ping
D                   # Request map data
I                   # Request player info
```

**Server to Client:**
```
OK                  # Command acknowledged
ERROR message       # Error response
MAP width height    # Map dimensions
...map data...
ENDMAP
PLAYERS count       # Player info
...player data...
ENDPLAYERS
TURN number         # Current turn
CURRENT player_id   # Current player
SHUTDOWN            # Server shutting down
```

### Telnet Negotiation

The frontend handles minimal RFC 854:
- **WILL SGA** - Suppress Go Ahead
- **DO SGA** - Accept SGA
- **WILL NAWS** - Negotiate About Window Size
- **DO NAWS** - Accept NAWS
- **DONT ECHO** - Server handles echo
- **DONT LINEMODE** - Character mode

IAC sequences are stripped from data before forwarding to PTY.

## Session Lifecycle

1. **Connection:** Client connects via telnet to port 5000
2. **Negotiation:** Frontend performs telnet option negotiation
3. **PTY Setup:** Frontend allocates PTY and forks
4. **Client Spawn:** Child process execs empire_client
5. **Login:** Client authenticates with server
6. **Gameplay:** Normal game interaction
7. **Disconnect:** Client quits or connection lost
8. **Cleanup:** Frontend reaps child, closes PTY

## Security Model

### Privilege Separation

**Frontend:**
- Runs as `empire` user
- Binds to port 5000 as root (if needed), then drops privileges
- No access to world data

**Server:**
- Runs as `empire` user
- Owns world data directory
- No network access beyond localhost

**Client:**
- Runs as `empire` user in PTY
- Minimal environment (sanitized)
- No direct filesystem access

### File Permissions

```
/var/lib/empire/world.dat    644 empire:empire
/var/log/empire/server.log   640 empire:empire
/var/run/empire/server.sock  660 empire:empire
/var/run/empire/server.pid   644 empire:empire
```

### Rate Limiting

- 100 connections per IP per 60-second window
- 30-minute idle timeout
- Maximum 64 concurrent connections

## Deployment

### Quick Start

```bash
# 1. Build everything
make clean && make

# 2. Setup directories and user
sudo ./setup.sh

# 3. Install binaries
sudo cp empire_server empire_frontend /usr/local/bin/

# 4. Start server manually
sudo -u empire /usr/local/bin/empire_server -d

# 5. Start frontend manually
sudo -u empire /usr/local/bin/empire_frontend -d

# 6. Connect
 telnet localhost 5000
```

### Systemd Deployment

```bash
# Install service files
sudo cp empire-server.service empire-frontend.service /etc/systemd/system/

# Reload systemd
sudo systemctl daemon-reload

# Enable services
sudo systemctl enable empire-server.service
sudo systemctl enable empire-frontend.service

# Start services
sudo systemctl start empire-server.service
sudo systemctl start empire-frontend.service

# Check status
sudo systemctl status empire-server.service
sudo systemctl status empire-frontend.service
```

### Monitoring

```bash
# View server logs
sudo tail -f /var/log/empire/server.log

# View systemd logs
sudo journalctl -u empire-server -f
sudo journalctl -u empire-frontend -f

# Check connections
ss -tlnp | grep empire

# List sessions
ps aux | grep empire_
```

## Comparison to Wolfpack Empire

This implementation provides similar architecture to empire.game-host.org:

| Feature | Wolfpack Empire | This Implementation |
|---------|----------------|---------------------|
| Telnet access | Yes | Yes (port 5000) |
| PTY isolation | Yes | Yes |
| Persistent world | Yes | Yes |
| Atomic saves | Yes | Yes |
| Rate limiting | Yes | Yes |
| Privilege separation | Yes | Yes |
| Multiple nations | Yes | Yes (up to 4) |
| Observer mode | Yes | Yes |

## Differences from Original

1. **No Direct Execution:** Unlike the original single-binary approach, game logic is isolated in the server
2. **Network Transparency:** Sessions can persist across disconnections
3. **Scalability:** Frontend can handle many concurrent connections
4. **Security:** Defense in depth with privilege separation
5. **Modern Protocol:** Text-based protocol instead of binary

## Troubleshooting

### "Connection refused"
- Check if frontend is running: `sudo systemctl status empire-frontend`
- Check if server is running: `sudo systemctl status empire-server`
- Verify port 5000 is listening: `ss -tlnp | grep 5000`

### "Cannot create world file"
- Check permissions on /var/lib/empire: `ls -la /var/lib/empire/`
- Ensure empire user owns directory
- Run setup.sh to fix permissions

### "Server not responding"
- Check server logs: `sudo tail /var/log/empire/server.log`
- Verify backend socket exists: `ls -la /var/run/empire/`
- Check for firewall blocking localhost:4000

### Client disconnects immediately
- Check frontend logs: `sudo journalctl -u empire-frontend`
- Verify empire_client binary exists and is executable
- Check PTY allocation: `ls /dev/pt*`

## Development

### Adding Protocol Commands

1. Define command constant in empire_server.c
2. Handle in process_command()
3. Add client-side support in empire_client.c
4. Update protocol documentation

### Modifying Game Logic

Game logic changes should be made in the core game files (attack.c, game.c, etc.). The server acts as a wrapper that manages sessions and persistence.

### Testing

```bash
# Test locally without systemd
./empire_server -p 4000 &
./empire_frontend -p 5000 -b 127.0.0.1 -P 4000 &
telnet localhost 5000
```

## License

Same as original Empire: GPL-2.0+
