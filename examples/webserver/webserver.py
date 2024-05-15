import os
import asyncio
import fs_async
from microdot import Microdot, send_file
from config import base_dir

extra_content_type_map = {
    'pdf': 'application/pdf',
    'svg': 'image/svg+xml',
}

class FileStream:
    def __init__(self, f):
        self.f = f

    def __aiter__(self):
        return self

    async def __anext__(self):
        buf = await self.f.read(0x8000)
        if len(buf) == 0:
            raise StopAsyncIteration
        return buf

    async def aclose(self):
        await self.f.close()


app = Microdot()

@app.route('/')
async def index(request):
    path = f'{base_dir}/index.html'
    f = await fs_async.open(path)
    return send_file(path, stream=FileStream(f))

@app.route('/<path:path>')
async def static(request, path):
    if '..' in path:
        # directory traversal is not allowed
        return 'Not found', 404

    path = f'{base_dir}/{path}'

    try:
        stat = await fs_async.stat(path)
    except:
        return 'File not found', 404

    max_age = 3600
    if stat[0] & 0o170000 == 0o40000: # directory
        path = f'{path}/index.html'
        max_age = 0

    content_type = None
    ext = path.split('.')[-1]
    if ext in extra_content_type_map:
        content_type = extra_content_type_map[ext]

    f = await fs_async.open(path)
    return send_file(path, stream=FileStream(f), content_type=content_type, max_age=max_age)

app.run(debug=True)