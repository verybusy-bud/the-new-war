# The New War - LBBS Door Setup

## Overview

The New War now supports multiplayer via TCP sockets. For LBBS integration, we provide a `door` wrapper that connects stdin/stdout to the game server.

## Architecture

```
[User Terminal] <--(stdin/stdout)--> [LBBS Door] <--(TCP)--> [Game Server]
                                          |
                                    (door executable)
```

## Setup

### 1. Start the Game Server

Run the game server first:

```bash
# For 2 players on port 7777
./tnw -p2 --server --port 7777

# For 4 players on port 7777
./tnw -p4 --server --port 7777

# Custom port
./tnw -p3 --server --port 8888
```

### 2. Configure LBBS Door

In your LBBS `doors.conf` or menu configuration:

```
exec /home/mainuser/codev/github/the-new-war/run-tnw-door
```

Or use environment variables:

```
export TNW_HOST=localhost
export TNW_PORT=7777
exec /home/mainuser/codev/github/the-new-war/door
```

### 3. Environment Variables

- `TNW_HOST`: Game server hostname (default: localhost)
- `TNW_PORT`: Game server port (default: 7777)

## File Structure

```
the-new-war/
├── tnw              # Game server (multiplayer)
├── door             # LBBS door wrapper
├── run-tnw-door     # Shell script for LBBS
└── DOOR-README.md   # This file
```

## Building

```bash
make clean && make
```

This creates:
- `tnw` - The game server
- `door` - The LBBS door wrapper

## Testing

### Test 1: Manual Connection

```bash
# Terminal 1: Start server
./tnw -p2 --server --port 7777

# Terminal 2: Connect via telnet
telnet localhost 7777

# Terminal 3: Connect second player
telnet localhost 7777
```

### Test 2: Via Door

```bash
# Terminal 1: Start server
./tnw -p2 --server --port 7777

# Terminal 2: Test door wrapper
echo "" | ./door localhost 7777
```

## Troubleshooting

### "Connection refused"
- Make sure the game server is running
- Check the port number matches

### "Cannot open saved game"
- This is normal for new games - it will create a new map

### Game never starts
- All players must connect first
- Each player must press ENTER at the title screen

## Multiplayer Notes

- The server waits for all players to connect before starting
- Each player connects independently through LBBS
- Game state is maintained server-side
- Players take turns - you see the map only on your turn

## Differences from Single-Player

- No ncurses display on server - all output goes to network clients
- Each player has their own view of the map
- Game persists until all players disconnect

## Credits

Based on VMS-Empire by Chuck Simmons (1987, 1988)
Modified for multiplayer by the community
