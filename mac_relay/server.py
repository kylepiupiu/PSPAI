#!/usr/bin/env python3
"""PSPAI ModelScope Mac Relay v0.2.2 — Python standard library only.

GET /health       ESP32-S3 status monitoring
POST /health      backwards compatible with PSP v0.1.9 diagnosis
POST /ask         plain text or JSON user question -> plain ASCII reply

AI runs remotely at ModelScope; ESP32-S3 only forwards TCP. PSP diagnostic
EBOOT.PBP does NOT implement /ask or text entry yet.
"""
from __future__ import annotations

from datetime import date
import http.client
import ssl
import socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import re
from html import escape as html_escape
from urllib.parse import urlsplit, parse_qs
import os
import threading
import time
import urllib.error
import urllib.request

PORT = int(os.environ.get('PSPAI_PORT', '8088'))
MODEL = os.environ.get('MODELSCOPE_MODEL', 'Qwen/Qwen3.5-27B').strip()
API_URL = 'https://api-inference.modelscope.cn/v1/chat/completions'
MAX_QUESTION_BYTES = 2500
MAX_ANSWER_CHARS = 900
MAX_RESPONSE_BYTES = 2048
DAILY_LOCAL_LIMIT = 40  # local per-process limit; NOT the provider's quota
MIN_GAP_SECONDS = 3
UPSTREAM_TIMEOUT = 60  # < 90s default ESP32 bridge idle timer
DEFAULT_CLIENT_IPS = '127.0.0.1,192.168.1.2,192.168.1.10'
ALLOWED_CLIENTS = {x.strip() for x in os.getenv('PSPAI_ALLOWED_CLIENTS', DEFAULT_CLIENT_IPS).split(',') if x.strip()}

_guard = threading.Lock()
_today = date.today()
_count = 0
_last = 0.0
_ai_busy = threading.Lock()

class AIError(Exception):
    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status


def _extract_chunk_content(obj):
    try:
        choice = obj['choices'][0]
        delta = choice.get('delta', choice.get('message', {}))
        part = delta.get('content')
        if isinstance(part, str):
            return part
        if isinstance(part, list):
            return ''.join(i.get('text', '') for i in part if isinstance(i, dict))
    except (KeyError, IndexError, AttributeError, TypeError):
        pass
    return ''


def decode_provider_response(resp):
    content_type = (resp.headers.get('Content-Type') or '').lower()
    chunks = []
    size = 0
    reasoning_seen = False
    if 'text/event-stream' in content_type:
        for raw_line in resp:
            line = raw_line.decode('utf-8', 'replace').strip()
            if not line.startswith('data:'):
                continue
            item = line[5:].strip()
            if item == '[DONE]':
                break
            if not item:
                continue
            try:
                data = json.loads(item)
            except ValueError:
                continue
            if isinstance(data, dict) and data.get('error'):
                raise AIError(502, 'Provider returned a streaming error')
            if isinstance(data, dict) and data.get('choices'):
                try:
                    if data['choices'][0].get('delta', {}).get('reasoning_content'):
                        reasoning_seen = True
                except (KeyError, IndexError, TypeError, AttributeError):
                    pass
            content = _extract_chunk_content(data)
            if content:
                chunks.append(content)
                size += len(content)
                if size >= MAX_ANSWER_CHARS * 4:
                    break
    else:
        raw = resp.read(512_001)
        if len(raw) > 512_000:
            raise AIError(502, 'Provider response too large')
        data = json.loads(raw.decode('utf-8'))
        if isinstance(data, dict) and data.get('error'):
            raise AIError(502, 'Provider rejected request')
        content = _extract_chunk_content(data)
        if content:
            chunks.append(content)
    answer = ''.join(chunks).strip()
    if not answer:
        message = ('Model returned thinking but no final answer; try disabling thinking or increasing max_tokens'
                   if reasoning_seen else 'Model returned no answer text; check model availability and response format')
        raise AIError(502, message)
    # Initial PSP client uses ASCII debug text; intentionally avoid raw CJK.
    answer = answer.encode('ascii', 'replace').decode('ascii')
    answer = ' '.join(answer.split())
    return answer[:MAX_ANSWER_CHARS]


def request_ai(question: str, token: str) -> str:
    payload = {
        'model': MODEL,
        'messages': [
            {'role': 'system', 'content': 'You are PSPAI, an assistant for a retro handheld. Reply in plain English using ASCII characters only. Answer in at most 450 characters, no Markdown, no emoji, no chain of thought.'},
            {'role': 'user', 'content': question},
        ],
        'max_tokens': 1024,
        'stream': True,
        # Qwen3.5 defaults to thinking; 700 tokens could finish before any answer.
        # Set PSPAI_DISABLE_THINKING=0 to omit this provider-specific argument.
        **({'chat_template_kwargs': {'enable_thinking': False}}
           if os.getenv('PSPAI_DISABLE_THINKING', '1') == '1' else {}),
    }
    req = urllib.request.Request(
        API_URL,
        data=json.dumps(payload, ensure_ascii=False).encode('utf-8'),
        headers={
            'Authorization': 'Bearer ' + token,
            'Content-Type': 'application/json',
            'Accept': 'text/event-stream',
        },
        method='POST',
    )
    try:
        with urllib.request.urlopen(req, timeout=UPSTREAM_TIMEOUT) as response:
            answer = decode_provider_response(response)
    except urllib.error.HTTPError as e:
        # Do not print the provider's raw error body: it could contain user input.
        if e.code in (401, 403):
            raise AIError(502, f'ModelScope HTTP {e.code}: token invalid, account unverified, or access forbidden') from e
        if e.code == 429:
            raise AIError(429, 'ModelScope HTTP 429: free quota or rate limit reached') from e
        if e.code in (400, 404, 422):
            raise AIError(502, f'ModelScope HTTP {e.code}: model ID or request parameters rejected') from e
        raise AIError(502, f'ModelScope HTTP {e.code}: upstream service returned an error') from e
    except urllib.error.URLError as e:
        reason = e.reason
        if isinstance(reason, ssl.SSLCertVerificationError):
            hint = 'SSL certificate verification failed (Python trust store or HTTPS interception)'
        elif isinstance(reason, (TimeoutError, socket.timeout)):
            hint = 'Connection timed out (network/proxy route)'
        elif isinstance(reason, socket.gaierror):
            hint = 'DNS resolution failed'
        else:
            hint = f'Network or proxy error ({type(reason).__name__})'
        raise AIError(502, f'ModelScope {hint}') from e
    except (TimeoutError, socket.timeout):
        raise AIError(502, 'ModelScope response timed out')
    except (ssl.SSLError, http.client.HTTPException, ConnectionResetError) as e:
        raise AIError(502, f'ModelScope TLS/HTTP transport failed ({type(e).__name__})') from e
    except (ValueError, UnicodeError, TypeError) as e:
        raise AIError(502, f'ModelScope response parsing failed ({type(e).__name__})') from e
    except OSError as e:
        raise AIError(502, f'ModelScope socket error ({type(e).__name__})') from e
    return answer


def reserve_request():
    global _today, _count, _last
    with _guard:
        today = date.today()
        now = time.monotonic()
        if today != _today:
            _today, _count, _last = today, 0, 0.0
        if _count >= DAILY_LOCAL_LIMIT:
            raise AIError(429, 'Local daily test limit reached')
        if now - _last < MIN_GAP_SECONDS:
            raise AIError(429, 'Wait 3 seconds before trying again')
        _last = now
        _count += 1


class Handler(BaseHTTPRequestHandler):
    server_version = 'PSPAI-ModelScope/0.3'

    def log_message(self, fmt, *args):
        # Browser GET carries the question in the URL: never log its contents.
        message = fmt % args
        message = re.sub(r'(\"GET /(?:chat|pspai))\?[^ ]+', r'\1?[redacted]', message)
        print('[PSPAI] %s %s' % (self.client_address[0], message), flush=True)

    def send_blob(self, status, body, content_type='text/plain; charset=utf-8'):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Connection', 'close')
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass
        self.close_connection = True

    def allowed(self):
        if self.client_address[0] not in ALLOWED_CLIENTS:
            self.send_blob(403, b'Client IP not allowed')
            return False
        return True

    def send_psp_chat(self, question='', answer='', error=''):
        # Minimal PSP browser fallback; no JS, external resources, or tokens in HTML.
        page = ('<!DOCTYPE html><html><head><meta http-equiv="Content-Type" '
                'content="text/html; charset=utf-8"><title>PSPAI</title>'
                '<style>body{font-family:sans-serif;background:#122133;color:#fff;'
                'font-size:17px;margin:12px}input{font-size:17px}a{color:#9ee} '
                'pre{white-space:pre-wrap;word-wrap:break-word}</style></head><body>'
                '<h2>PSPAI v0.3 - Qwen</h2>'
                '<form action="/chat" method="get">'
                '<p><input type="text" name="q" size="28" maxlength="120" value="' +
                html_escape(question, quote=True) + '"></p>'
                '<p><input type="submit" value="Ask AI"></p></form>'
                '<p><a href="/chat">Clear</a></p>')
        if error:
            page += '<p>ERROR: ' + html_escape(error) + '</p>'
        if answer:
            page += '<hr><h3>AI Answer</h3><pre>' + html_escape(answer) + '</pre>'
        page += '</body></html>'
        self.send_blob(200, page.encode('utf-8'), 'text/html; charset=utf-8')

    def do_GET(self):
        if not self.allowed():
            return
        route = urlsplit(self.path)
        if route.path in ('/chat', '/pspai'):
            values = parse_qs(route.query, keep_blank_values=True)
            question = values.get('q', [''])[0].strip()
            if not question:
                self.send_psp_chat()
                return
            if len(question) > 120:
                self.send_psp_chat(error='Question exceeds 120 characters')
                return
            token = os.getenv('MODELSCOPE_TOKEN', '').strip()
            if not token:
                self.send_psp_chat(question, error='ModelScope token not set')
                return
            if not _ai_busy.acquire(blocking=False):
                self.send_psp_chat(question, error='AI busy - please retry')
                return
            try:
                reserve_request()
                answer = request_ai(question, token)
                self.send_psp_chat(question, answer=answer)
                print('[PSPAI] browser_reply=200 bytes=%d' % len(answer), flush=True)
            except AIError as error:
                print(f'[PSPAI] browser_error={error.status} {error}', flush=True)
                self.send_psp_chat(question, error=str(error))
            finally:
                _ai_busy.release()
            return
        if route.path == '/health':
            body = json.dumps({
                'status': 'ok',
                'ai_configured': bool(os.environ.get('MODELSCOPE_TOKEN')),
                'provider': 'modelscope',
                'model': MODEL,
            }, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
            self.send_blob(200, body, 'application/json; charset=utf-8')
        else:
            self.send_blob(404, b'Not found')

    def do_POST(self):
        if not self.allowed():
            return
        try:
            length = int(self.headers.get('Content-Length', '-1'))
        except ValueError:
            self.send_blob(400, b'Invalid Content-Length')
            return
        if not 0 <= length <= MAX_QUESTION_BYTES:
            self.send_blob(413, b'Request too large or missing Content-Length')
            return
        raw = self.rfile.read(length)
        path = self.path.split('?', 1)[0]
        if path == '/health':
            self.send_blob(200, b'MAC RELAY ONLINE')
            return
        if path != '/ask':
            self.send_blob(404, b'Not found')
            return
        try:
            if 'application/json' in self.headers.get('Content-Type', ''):
                question = json.loads(raw.decode('utf-8'))['question']
            else:
                question = raw.decode('utf-8')
            if not isinstance(question, str) or not question.strip():
                raise ValueError('Empty question')
            question = question.strip()
        except (UnicodeError, ValueError, KeyError, TypeError):
            self.send_blob(400, b'Invalid question')
            return
        token = os.getenv('MODELSCOPE_TOKEN', '').strip()
        if not token:
            self.send_blob(503, b'ModelScope token not set')
            return
        if not _ai_busy.acquire(blocking=False):
            self.send_blob(429, b'AI busy: try again shortly')
            return
        try:
            reserve_request()
            t0 = time.monotonic()
            answer = request_ai(question, token)
            body = answer.encode('utf-8')[:MAX_RESPONSE_BYTES]
            print(f'[PSPAI] model_reply=200 bytes={len(body)} elapsed={time.monotonic()-t0:.1f}s', flush=True)
            self.send_blob(200, body)
        except AIError as error:
            print(f'[PSPAI] model_error={error.status} {error}', flush=True)
            self.send_blob(error.status, str(error).encode('utf-8'))
        finally:
            _ai_busy.release()


def run():
    print(f'PSPAI Mac Relay v0.2.2 on 0.0.0.0:{PORT}', flush=True)
    print(f'ModelScope: {MODEL}; token configured: {bool(os.getenv("MODELSCOPE_TOKEN"))}', flush=True)
    print(f'Allowed IPs: {", ".join(sorted(ALLOWED_CLIENTS))}', flush=True)
    print('GET /chat, GET/POST /health, POST /ask ready', flush=True)
    ThreadingHTTPServer(('0.0.0.0', PORT), Handler).serve_forever()


if __name__ == '__main__':
    run()
