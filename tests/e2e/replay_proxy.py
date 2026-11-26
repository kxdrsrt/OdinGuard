#!/usr/bin/env python3
"""TCP proxy for the replay test: forwards a map-server connection and, AFTER seconds after the connection was
opened, sends one client chunk (whole encrypted OdinGuard records, as captured on the wire) a second time.

    replay_proxy.py LISTEN_PORT TARGET_HOST TARGET_PORT AFTER_SECONDS

Prints "replayed N bytes" once. Serves connections until it is killed."""
import socket
import sys
import threading
import time

listen_port, host, port, after = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), float(sys.argv[4])
replayed = threading.Event()


def pipe(src, dst, client_side, opened):
    try:
        while True:
            data = src.recv(65536)
            if not data:
                break
            dst.sendall(data)
            if client_side and not replayed.is_set() and time.time() - opened >= after:
                replayed.set()
                time.sleep(0.2)
                dst.sendall(data)
                print(f"replayed {len(data)} bytes", flush=True)
    except OSError:
        pass
    finally:
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", listen_port))
srv.listen(4)
while True:
    c, _ = srv.accept()
    u = socket.create_connection((host, port))
    t = time.time()
    threading.Thread(target=pipe, args=(c, u, True, t), daemon=True).start()
    threading.Thread(target=pipe, args=(u, c, False, t), daemon=True).start()
