#!/usr/bin/env python3
"""PSPAI v0.3 offline HTTP protocol checks; no ModelScope network calls."""
import os
import threading
import unittest
import urllib.request
from http.server import ThreadingHTTPServer
from unittest import mock
import server


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.http = ThreadingHTTPServer(('127.0.0.1', 0), server.Handler)
        cls.base = 'http://127.0.0.1:%d' % cls.http.server_port
        cls.thread = threading.Thread(target=cls.http.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.http.shutdown(); cls.http.server_close(); cls.thread.join(timeout=2)

    def test_browser_home(self):
        with urllib.request.urlopen(self.base + '/chat', timeout=2) as res:
            body = res.read().decode('utf-8')
            self.assertEqual(res.status, 200)
            self.assertIn('PSPAI v0.3', body)
            self.assertIn('method="get"', body)
            self.assertIn('name="q"', body)
            self.assertNotIn('fetch(', body)

    def test_browser_model_mock(self):
        with mock.patch.dict(os.environ, {'MODELSCOPE_TOKEN':'fake'}), \
             mock.patch.object(server, 'reserve_request'), \
             mock.patch.object(server, 'request_ai', return_value='HELLO PSPAI') as model:
            with urllib.request.urlopen(self.base + '/chat?q=hello%20PSPAI',timeout=2) as res:
                body = res.read().decode('utf-8')
                self.assertIn('HELLO PSPAI', body)
            model.assert_called_once_with('hello PSPAI', 'fake')

    def test_html_escapes(self):
        with mock.patch.dict(os.environ, {'MODELSCOPE_TOKEN':'fake'}), \
             mock.patch.object(server, 'reserve_request'), \
             mock.patch.object(server, 'request_ai', return_value='<script>alert(1)</script>'):
            with urllib.request.urlopen(self.base + '/pspai?q=%3Cb%3E',timeout=2) as res:
                body = res.read().decode('utf-8')
                self.assertNotIn('<script>', body)
                self.assertIn('&lt;script&gt;', body)
                self.assertIn('&lt;b&gt;', body)

    def test_psp_raw_ask_http10(self):
        # Reproduce the bare HTTP/1.0 transaction produced by psp/main.c.
        with mock.patch.dict(os.environ, {'MODELSCOPE_TOKEN':'fake'}), \
             mock.patch.object(server, 'reserve_request'), \
             mock.patch.object(server, 'request_ai', return_value='A PSP AI answer.'):
            import socket
            with socket.create_connection(('127.0.0.1', self.http.server_port),timeout=2) as s:
                s.sendall(b'POST /ask HTTP/1.0\r\nHost: 192.168.4.1:8088\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 5\r\nConnection: close\r\n\r\nHELLO')
                s.settimeout(2)
                data = b''
                while True:
                    b = s.recv(4096)
                    if not b: break
                    data += b
            self.assertIn(b'200 OK', data)
            self.assertIn(b'\r\n\r\nA PSP AI answer.', data)


if __name__=='__main__':
    unittest.main(verbosity=2)
