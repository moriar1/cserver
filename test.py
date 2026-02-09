import requests
from pathlib import Path
import time

time.sleep(3)  # Ensure cserver has enough time to start up

# GET => 200 OK
try:
    res = requests.get("http://localhost:3490", timeout=5)
except requests.exceptions.ConnectionError:
    exit("Server is down [GET]")

assert res.status_code == 200
assert res.text == Path("index.html").read_text()

# POST => 404 NOT FOUND
try:
    res = requests.post("http://localhost:3490", timeout=5)
except requests.exceptions.ConnectionError:
    exit("Server is down [POST]")

assert res.status_code == 404
