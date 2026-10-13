#!/usr/bin/env python3
"""Call local Relay once; this uses one ModelScope online request."""
import sys
import urllib.request
import urllib.error

question = ' '.join(sys.argv[1:]).strip() if len(sys.argv)>1 else 'Say HELLO PSPAI in one short English sentence.'
req = urllib.request.Request('http://127.0.0.1:8088/ask', data=question.encode('utf-8'), headers={'Content-Type': 'text/plain; charset=utf-8'}, method='POST')
try:
    with urllib.request.urlopen(req, timeout=75) as response:
        print('HTTP', response.status)
        print('Answer:', response.read().decode('utf-8', 'replace'))
except urllib.error.HTTPError as e:
    print('HTTP', e.code, e.read().decode('utf-8', 'replace'))
    sys.exit(1)
except Exception as e:
    print('Relay connection error:', e)
    sys.exit(2)
