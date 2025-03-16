#!/bin/bash
# Script to restart the server with fresh logs

# Make sure the script is executable
chmod +x "$0"

# Kill any running server instances
pkill -f chatApp || true

# Wait for processes to terminate
sleep 1

# Start server with specified port
./chatApp $1 &

# Show initial log entries
echo "Server started. Initial log entries:"
tail -f chat_server.log