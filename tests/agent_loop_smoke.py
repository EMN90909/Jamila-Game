#!/usr/bin/env python3
"""Hermetic model+MCP simulation: NOT a test of a real Godot binary or credentials."""
import http.server
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading

APP, MOCK = sys.argv[1:]
class Handler(http.server.BaseHTTPRequestHandler):
    scouts = 0
    def log_message(self, *args):
        pass
    def do_POST(self):
        n=int(self.headers.get('content-length','0'))
        payload=json.loads(self.rfile.read(n))
        messages=payload.get('messages',[])
        anthropic='max_tokens' in payload
        if anthropic:
            assert 'system' in payload, 'Anthropic requires top-level system'
            tools={tool['name'] for tool in payload.get('tools',[])}
            called=[item for m in messages for item in (m.get('content') if isinstance(m.get('content'),list) else []) if item.get('type')=='tool_result']
        else:
            tools={tool['function']['name'] for tool in payload.get('tools',[])}
            called=[m for m in messages if m.get('role')=='tool']
        if not payload.get('tools'):
            Handler.scouts+=1
            result={"role":"assistant","content":"Suggest one small change and verify it."}
        else:
            assert {'jamila__write_file','jamila__read_file','get_project_info'} <= tools, tools
            number=len(called)
            if number==0:
                name='jamila__write_file'
                args={'path':'scripts/player.gd','content':'extends CharacterBody2D\nfunc _physics_process(delta: float) -> void:\n    velocity.y += 9.8 * delta\n'}
            elif number==1:
                name='jamila__read_file'
                args={'path':'scripts/player.gd'}
            elif number==2:
                name='get_project_info'
                args={'projectPath':Handler.project}
            else:
                text='Coding workflow finished. Mock scene tree obtained, real Godot engine not tested.'
                result={'role':'assistant','content':text}
                if anthropic:
                    data=json.dumps({'content':[{'type':'text','text':text}]}).encode()
                else:
                    data=json.dumps({'choices':[{'message':result}]}).encode()
                self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
                return
            result={'role':'assistant','content':None,'tool_calls':[{'id':f'tool{number}','type':'function','function':{'name':name,'arguments':json.dumps(args)}}]}
        if anthropic:
            if result.get('tool_calls'):
                call=result['tool_calls'][0]
                body={'content':[{'type':'tool_use','id':call['id'],'name':call['function']['name'],'input':json.loads(call['function']['arguments'])}]}
            else: body={'content':[{'type':'text','text':result['content']}]}
            data=json.dumps(body).encode()
        else: data=json.dumps({'choices':[{'message':result}]}).encode()
        self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)

with tempfile.TemporaryDirectory() as directory:
    root=pathlib.Path(directory)
    (root/'project.godot').write_text('config_version=5\n')
    # Deliberately omit scripts/ to verify the native writer creates it securely.
    env=dict(os.environ)
    env['JAMILA_SMOKE_PROJECT']=str(root)
    Handler.project=str(root)
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    try:
        env['JAMILA_API_URL']=f'http://127.0.0.1:{server.server_port}/v1/chat/completions'
        env['JAMILA_API_KEY']=''
        args=[APP,'--workspace',str(root),'--mcp-bin',MOCK,'--ask','Fix character gravity using Godot MCP and verify the script.','--team']
        approved=subprocess.run(args+['--approval-mode','allow-all'],env=env,text=True,capture_output=True,timeout=30)
        assert approved.returncode==0,(approved.stdout,approved.stderr)
        assert 'Mock scene tree' in approved.stdout or 'Mock scene tree' in approved.stdout.title() or 'Mock scene' in approved.stdout or 'Mock scene tree obtained' in approved.stdout
        assert 'velocity.y += 9.8' in (root/'scripts/player.gd').read_text()
        assert Handler.scouts>=3,f'Parallel advisors did not run ({Handler.scouts})'
        (root/'scripts/player.gd').unlink()
        blocked=subprocess.run(args[:-1]+['--approval-mode','human'],env=env,text=True,capture_output=True,timeout=30)
        assert blocked.returncode==0,(blocked.stdout,blocked.stderr)
        assert not (root/'scripts/player.gd').exists(),'Headless Human Approval unexpectedly wrote a file'
        claude=subprocess.run(args[:-1]+['--provider','anthropic','--approval-mode','allow-all'],env=env,text=True,capture_output=True,timeout=30)
        assert claude.returncode==0,(claude.stdout,claude.stderr)
        assert (root/'scripts/player.gd').exists(),'Anthropic tool_use -> native write was not executed'
    finally:
        server.shutdown();server.server_close()
print('Mock Godot MCP + OpenAI/Claude API workflows: script write/read/project inspection/parallel agents + headless approval gating PASS (NOT live providers/Godot)')