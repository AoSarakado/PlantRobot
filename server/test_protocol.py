"""HTTP contract smoke test without downloading an ASR model."""

from http.server import HTTPServer
from threading import Thread
from types import SimpleNamespace
from urllib.request import Request, urlopen

from asr_server import make_handler


class StubModel:
    def transcribe(self, audio, **kwargs):
        assert len(audio) == 16000
        assert kwargs["language"] == "zh"
        return iter([SimpleNamespace(text="你好", no_speech_prob=0.1)]), None


server = HTTPServer(("127.0.0.1", 0), make_handler(StubModel()))
thread = Thread(target=server.handle_request)
thread.start()
request = Request(
    f"http://127.0.0.1:{server.server_port}/transcribe",
    data=b"\0\0" * 16000,
    headers={"Content-Type": "application/octet-stream"},
    method="POST",
)
with urlopen(request, timeout=5) as response:
    assert response.status == 200
    assert response.read().decode("utf-8") == "你好"
thread.join(timeout=5)
server.server_close()
print("ASR HTTP contract OK")
