#!/usr/bin/env python3
"""Safe ModelScope connection check; no token, no API request charges."""
import socket
import urllib.request
import urllib.error
import ssl

HOST = "api-inference.modelscope.cn"
URL = "https://api-inference.modelscope.cn/v1/chat/completions"
print("ModelScope endpoint:", URL)
try:
    addresses = sorted({item[4][0] for item in socket.getaddrinfo(HOST, 443, type=socket.SOCK_STREAM)})
    print("DNS OK:", ", ".join(addresses[:5]))
except Exception as e:
    print("DNS FAIL:", type(e).__name__, str(e)[:150])
    raise SystemExit(2)
try:
    req = urllib.request.Request(URL, method="GET")
    with urllib.request.urlopen(req, timeout=12) as resp:
        print("HTTPS reachable: HTTP", resp.status)
except urllib.error.HTTPError as e:
    print("HTTPS reachable: HTTP", e.code, "(GET rejected is normal for a POST API)")
except Exception as e:
    print("HTTPS connection failed:", type(e).__name__, str(e)[:180])
    raise SystemExit(1)
