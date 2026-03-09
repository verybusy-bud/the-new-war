#!/bin/bash
#
# Setup script for Empire multi-user server
# Run as root to create necessary directories and permissions
#

set -e

EMPIRE_USER="empire"
EMPIRE_GROUP="empire"

# Create empire user and group
echo "Creating empire user and group..."
if ! getent group "$EMPIRE_GROUP" > /dev/null 2>&1; then
    groupadd --system "$EMPIRE_GROUP"
fi

if ! id "$EMPIRE_USER" > /dev/null 2>&1; then
    useradd --system --gid "$EMPIRE_GROUP" --shell /bin/false \
            --home-dir /var/lib/empire --no-create-home "$EMPIRE_USER"
fi

# Create directories
echo "Creating directories..."
mkdir -p /var/lib/empire
mkdir -p /var/log/empire
mkdir -p /var/run/empire

# Set ownership and permissions
echo "Setting permissions..."
chown "$EMPIRE_USER:$EMPIRE_GROUP" /var/lib/empire
chown "$EMPIRE_USER:$EMPIRE_GROUP" /var/log/empire
chown "$EMPIRE_USER:$EMPIRE_GROUP" /var/run/empire

chmod 755 /var/lib/empire
chmod 755 /var/log/empire
chmod 755 /var/run/empire

# Create log file with proper permissions
touch /var/log/empire/server.log
chown "$EMPIRE_USER:$EMPIRE_GROUP" /var/log/empire/server.log
chmod 640 /var/log/empire/server.log

echo "Setup complete."
echo ""
echo "Next steps:"
echo "1. Copy binaries to /usr/local/bin:"
echo "   sudo cp empire_server empire_frontend empire_client /usr/local/bin/"
echo "2. Copy systemd service files:"
echo "   sudo cp empire-server.service empire-frontend.service /etc/systemd/system/"
echo "3. Enable and start services:"
echo "   sudo systemctl daemon-reload"
echo "   sudo systemctl enable empire-server.service"
echo "   sudo systemctl enable empire-frontend.service"
echo "   sudo systemctl start empire-server.service"
echo "   sudo systemctl start empire-frontend.service"
echo "4. Connect with telnet:"
echo "   telnet localhost 5000"
