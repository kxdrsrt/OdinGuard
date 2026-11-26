#!/usr/bin/env python3
"""Stand-in for a web panel's crash endpoint: logs every POST (token, file name, size) as one line to OUT.

    crash_receiver.py PORT OUT"""
import socketserver
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

port, out = int(sys.argv[1]), sys.argv[2]


class H(BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        with open(out, "a") as f:
            f.write(f"{self.path} token={self.headers.get('X-OdinGuard-Token', '')} file={self.headers.get('X-OdinGuard-File', '')} "
                    f"bytes={len(body)} first={body[:24]!r}\n")
        self.send_response(200)
        self.end_headers()

    def log_message(self, *a):
        pass


class Server(HTTPServer):
    def server_bind(self):
        # HTTPServer.server_bind resolves the host name (getfqdn), which can hang for a long time on macOS
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = "localhost", port


Server(("0.0.0.0", port), H).serve_forever()
