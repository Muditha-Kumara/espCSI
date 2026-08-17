#!/usr/bin/env python3
"""
OTA Server for ESP-CSI System
Serves firmware binaries and version info for OTA updates

Usage:
    python3 ota_server.py --port 8070

Directory structure:
    firmware/
        receiver.bin
        broadcaster.bin
    version/
        receiver.txt
        broadcaster.txt
"""

import http.server
import socketserver
import os
import sys
import argparse
from pathlib import Path

# Default paths
FIRMWARE_DIR = Path("firmware")
VERSION_DIR = Path("version")

class OTAHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP handler for OTA requests"""

    def do_GET(self):
        """Handle GET requests"""

        # Version endpoints
        if self.path == "/version/receiver":
            self._serve_version("receiver")
        elif self.path == "/version/broadcaster":
            self._serve_version("broadcaster")

        # Firmware endpoints
        elif self.path == "/firmware/receiver":
            self._serve_firmware("receiver")
        elif self.path == "/firmware/broadcaster":
            self._serve_firmware("broadcaster")

        # Status endpoint
        elif self.path == "/status":
            self._serve_status()

        else:
            self.send_error(404, "Not Found")

    def _serve_version(self, device):
        """Serve version file for device"""
        version_file = VERSION_DIR / f"{device}.txt"

        if not version_file.exists():
            self.send_error(404, f"Version file not found: {device}")
            return

        try:
            version = version_file.read_text().strip()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(version)))
            self.end_headers()
            self.wfile.write(version.encode())
            self.log_message(f"Served version for {device}: {version}")
        except Exception as e:
            self.send_error(500, f"Error reading version: {e}")

    def _serve_firmware(self, device):
        """Serve firmware binary for device"""
        firmware_file = FIRMWARE_DIR / f"{device}.bin"

        if not firmware_file.exists():
            self.send_error(404, f"Firmware file not found: {device}")
            return

        try:
            file_size = firmware_file.stat().st_size

            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(file_size))
            self.send_header("Content-Disposition", f'attachment; filename="{device}.bin"')
            self.end_headers()

            with open(firmware_file, "rb") as f:
                while True:
                    chunk = f.read(8192)
                    if not chunk:
                        break
                    self.wfile.write(chunk)

            self.log_message(f"Served firmware for {device}: {file_size} bytes")
        except Exception as e:
            self.send_error(500, f"Error serving firmware: {e}")

    def _serve_status(self):
        """Serve server status"""
        status = {
            "server": "running",
            "firmware": {},
            "versions": {}
        }

        # Check firmware files
        for device in ["receiver", "broadcaster"]:
            fw_file = FIRMWARE_DIR / f"{device}.bin"
            ver_file = VERSION_DIR / f"{device}.txt"

            status["firmware"][device] = {
                "exists": fw_file.exists(),
                "size": fw_file.stat().st_size if fw_file.exists() else 0
            }

            if ver_file.exists():
                status["versions"][device] = ver_file.read_text().strip()
            else:
                status["versions"][device] = None

        import json
        response = json.dumps(status, indent=2)

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response.encode())

    def log_message(self, format, *args):
        """Custom log format"""
        sys.stderr.write(f"[{self.log_date_time_string()}] {format % args}\n")
        sys.stderr.flush()


def create_sample_files():
    """Create sample version files if they don't exist"""
    VERSION_DIR.mkdir(exist_ok=True)
    FIRMWARE_DIR.mkdir(exist_ok=True)

    for device in ["receiver", "broadcaster"]:
        version_file = VERSION_DIR / f"{device}.txt"
        if not version_file.exists():
            version_file.write_text("1.0.0\n")
            print(f"Created sample version file: {version_file}")

    print("\nNote: Place your firmware binaries in the 'firmware/' directory:")
    print("  - firmware/receiver.bin")
    print("  - firmware/broadcaster.bin")
    print("\nUpdate version files when you build new firmware:")
    print("  - version/receiver.txt")
    print("  - version/broadcaster.txt")


def main():
    parser = argparse.ArgumentParser(description="ESP-CSI OTA Server")
    parser.add_argument("--port", type=int, default=8070,
                       help="Port to listen on (default: 8070)")
    parser.add_argument("--host", type=str, default="0.0.0.0",
                       help="Host to bind to (default: 0.0.0.0)")
    parser.add_argument("--init", action="store_true",
                       help="Create sample directory structure and exit")

    args = parser.parse_args()

    if args.init:
        create_sample_files()
        return

    # Check if directories exist
    if not FIRMWARE_DIR.exists() or not VERSION_DIR.exists():
        print("Warning: firmware/ or version/ directories not found.")
        print("Run with --init to create sample structure.\n")
        create_sample_files()
        print()

    # Start server
    with socketserver.TCPServer((args.host, args.port), OTAHandler) as httpd:
        print(f"ESP-CSI OTA Server")
        print(f"Listening on http://{args.host}:{args.port}")
        print(f"Firmware directory: {FIRMWARE_DIR.absolute()}")
        print(f"Version directory: {VERSION_DIR.absolute()}")
        print("\nEndpoints:")
        print(f"  GET /version/receiver      - Receiver version")
        print(f"  GET /version/broadcaster   - Broadcaster version")
        print(f"  GET /firmware/receiver     - Receiver firmware binary")
        print(f"  GET /firmware/broadcaster  - Broadcaster firmware binary")
        print(f"  GET /status                - Server status (JSON)")
        print("\nPress Ctrl+C to stop\n")

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")
            sys.exit(0)


if __name__ == "__main__":
    main()
