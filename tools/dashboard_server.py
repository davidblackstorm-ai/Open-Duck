from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from functools import partial
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'dashboard'
print('OpenDuck dashboard: http://127.0.0.1:8769',flush=True)
ThreadingHTTPServer(('127.0.0.1',8769),partial(SimpleHTTPRequestHandler,directory=str(root))).serve_forever()
