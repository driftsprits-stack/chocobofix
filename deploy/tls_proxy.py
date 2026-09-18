#!/usr/bin/env python3
"""A minimal TLS-terminating reverse proxy, for TESTING the hosted shape only.

Production uses Caddy or nginx (deploy/Caddyfile, deploy/nginx.conf). This exists
so the deployment shape - browser --HTTPS--> proxy --HTTP--> loopback service -
can be exercised on a machine that has neither installed, which is how the path
in docs/DEPLOYMENT.md was actually verified.

Do not deploy this. It has no rate limiting, no access logging, no certificate
management, and no hardening.

usage: tls_proxy.py --cert cert.pem --key key.pem --listen 8443 --upstream 8080
"""
import argparse, socket, ssl, threading

def pipe(a, b):
    try:
        while True:
            data = a.recv(65536)
            if not data: break
            b.sendall(data)
    except Exception:
        pass
    finally:
        for s in (a, b):
            try: s.shutdown(socket.SHUT_RDWR)
            except Exception: pass
            try: s.close()
            except Exception: pass

def serve(args):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(args.cert, args.key)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', args.listen))
    srv.listen(64)
    print(f'TLS proxy on https://127.0.0.1:{args.listen} -> http://127.0.0.1:{args.upstream}', flush=True)
    while True:
        try:
            raw, _ = srv.accept()
            tls = ctx.wrap_socket(raw, server_side=True)
        except Exception:
            continue
        up = socket.create_connection(('127.0.0.1', args.upstream))
        threading.Thread(target=pipe, args=(tls, up), daemon=True).start()
        threading.Thread(target=pipe, args=(up, tls), daemon=True).start()

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--cert', required=True)
    p.add_argument('--key', required=True)
    p.add_argument('--listen', type=int, default=8443)
    p.add_argument('--upstream', type=int, default=8080)
    serve(p.parse_args())
