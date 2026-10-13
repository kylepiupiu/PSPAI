#!/usr/bin/env python3
"""Interactive Mac startup; token remains only in this process environment."""
import os
from getpass import getpass
from server import run

if __name__ == '__main__':
    if not os.getenv('MODELSCOPE_TOKEN'):
        print('ModelScope token: https://modelscope.cn/my/myaccesstoken')
        print('Input is hidden. Do not paste your token into chat or code.')
        value = getpass('Paste NEW ModelScope token: ').strip()
        if not value:
            raise SystemExit('No token entered; Relay not started.')
        os.environ['MODELSCOPE_TOKEN'] = value
    run()
