import requests
from pathlib import Path

# GET => 200
try:
    res = requests.get("http://localhost:3490", timeout=5)
except requests.exceptions.ConnectionError:
    exit("Server is down")
assert res.status_code == 200, res.status_code
assert res.text == Path("index.html").read_text()

# Too long path => 400
try:
    res = requests.get("http://localhost:3490/" + ("1" * 512), timeout=5)
except requests.exceptions.ConnectionError:
    exit("Server is down")
assert res.status_code == 400, res.status_code

# ../ (Path Traversal) => 403
try:
    res = requests.get("http://localhost:3490/\../", timeout=5)
except requests.exceptions.ConnectionError:
    exit("Server is down")
assert res.status_code == 403, res.status_code

# Wrong path => 404
try:
    res = requests.get("http://localhost:3490/file_not_exists.txtt", timeout=5)
except requests.exceptions.ConnectionError:
    exit("Server is down")
assert res.status_code == 404, res.status_code

# Too long headers => 431
try:
    headers = {"Large-Header": "a" * 4096}
    res = requests.post("http://localhost:3490", timeout=5, headers=headers)
except requests.exceptions.ConnectionError:
    exit("Server is down")
assert res.status_code == 431, res.status_code
