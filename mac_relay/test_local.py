#!/usr/bin/env python3
"""Offline self-check; no account and no paid/free cloud calls are made."""
import io
import json
import os
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from unittest import mock
import server

class FakeResponse:
    def __init__(self, lines=None, raw=b'', kind='text/event-stream'):
        self.lines = lines or []
        self.raw = raw
        self.headers = {'Content-Type':kind}
    def __iter__(self):
        return iter(self.lines)
    def read(self, n=0):
        return self.raw

class Basic(unittest.TestCase):
    def test_sse(self):
        payloads = [b': keepalive\n', b'data: {"choices":[{"delta":{"reasoning_content":"secret reasoning"}}]}\n', b'data: {"choices":[{"delta":{"content":"HELLO "}}]}\n', b'data: {"choices":[{"delta":{"content":"PSPAI"}}]}\n', b'data: [DONE]\n']
        self.assertEqual(server.decode_provider_response(FakeResponse(payloads)), 'HELLO PSPAI')
    def test_normal_json(self):
        v = b'{"choices":[{"message":{"content":"Hello PSPAI"}}]}'
        self.assertEqual(server.decode_provider_response(FakeResponse(raw=v,kind='application/json')), 'Hello PSPAI')
    def test_empty_response(self):
        with self.assertRaises(server.AIError):
            server.decode_provider_response(FakeResponse([b'data: [DONE]\n']))
    def test_ascii_sanitizer(self):
        payload = b'{"choices":[{"message":{"content":"Hi \\u4f60\\u597d"}}]}'
        self.assertTrue(server.decode_provider_response(FakeResponse(raw=payload,kind='application/json')).startswith('Hi'))

class Endpoints(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.http = ThreadingHTTPServer(('127.0.0.1',0),server.Handler)
        cls.base = f'http://127.0.0.1:{cls.http.server_port}'
        cls.thread = threading.Thread(target=cls.http.serve_forever,daemon=True)
        cls.thread.start()
    @classmethod
    def tearDownClass(cls):
        cls.http.shutdown()
        cls.http.server_close()
        cls.thread.join(timeout=3)
    def request(self, path, body=None, ctype='text/plain'):
        req=urllib.request.Request(self.base+path,data=body,headers={'Content-Type':ctype} if body is not None else {},method='POST' if body is not None else 'GET')
        with urllib.request.urlopen(req,timeout=2) as resp:
            return resp.status, resp.read()
    def test_health_get(self):
        with mock.patch.dict(os.environ, {'MODELSCOPE_TOKEN': 'fake-secret'}):
            code, body = self.request('/health')
            data = json.loads(body)
            self.assertEqual((code,data['status'],data['ai_configured']), (200,'ok',True))
            self.assertNotIn(b'fake-secret',body)
    def test_health_post(self):
        code,body=self.request('/health',b'ping')
        self.assertEqual((code,body),(200,b'MAC RELAY ONLINE'))
    def test_ask_mock(self):
        with mock.patch.dict(os.environ, {'MODELSCOPE_TOKEN': 'dummy'}):
            with mock.patch.object(server,'request_ai',return_value='HELLO PSPAI') as model:
                with mock.patch.object(server,'reserve_request'):
                    code, body = self.request('/ask', b'Hi PSP')
                    self.assertEqual((code,body),(200,b'HELLO PSPAI'))
                    self.assertEqual(model.call_args.args[0],'Hi PSP')
    def test_ask_not_configured(self):
        with mock.patch.dict(os.environ,{},clear=True):
            req=urllib.request.Request(self.base+'/ask',data=b'hello',method='POST')
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(req,timeout=2)
            self.assertEqual(ctx.exception.code, 503)
    def test_missing_question(self):
        with mock.patch.dict(os.environ,{'MODELSCOPE_TOKEN':'dummy'}):
            req=urllib.request.Request(self.base+'/ask',data=b'   ',method='POST')
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(req,timeout=2)
            self.assertEqual(ctx.exception.code,400)
    def test_missing_path(self):
        req=urllib.request.Request(self.base+'/unknown')
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            urllib.request.urlopen(req,timeout=2)
        self.assertEqual(ctx.exception.code,404)

if __name__=='__main__':
    unittest.main(verbosity=2)
