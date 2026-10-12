"""Save public user/assistant text from this chat, excluding internal instructions/tools."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SESSION=Path(r'C:\Users\Sumi\.codex\sessions\2026\10\11\rollout-2026-10-11T17-49-50-01a12a5e-60b0-7ee2-968d-7e3240b21836.jsonl')
def main():
    messages=[]
    for line in SESSION.read_text(encoding='utf-8').splitlines():
        try:item=json.loads(line)
        except json.JSONDecodeError:continue
        if item.get('type')!='response_item':continue
        payload=item.get('payload',{})
        if payload.get('type')!='message' or payload.get('role') not in ('user','assistant'):continue
        if payload.get('channel') not in (None,'final','commentary'):continue
        parts=[c.get('text','') for c in payload.get('content',[]) if c.get('type') in ('input_text','output_text')]
        text='\n'.join(parts)
        if not text or text.startswith(('<environment_context>','<external_codex_apps_open_page>')):continue
        messages.append({'timestamp':item.get('timestamp'),'role':payload['role'],'channel':payload.get('channel'),'text':text})
    (ROOT/'docs/chat_messages.json').write_text(json.dumps(messages,ensure_ascii=False,indent=2),encoding='utf-8')
    output=['# 本聊天公开对话记录','',
      '从本机聊天记录只读导出用户消息及助手公开回复；不含系统指令、内部推理、工具输出。记录截至本次导出时刻。','']
    for n,m in enumerate(messages,1):output.extend([f"## {n}. {'用户' if m['role']=='user' else '助手'} · {m.get('timestamp','')}",'',m['text'],''])
    (ROOT/'docs/本聊天记录.md').write_text('\n'.join(output),encoding='utf-8')
    print(f'Exported {len(messages)} public messages to simulation/docs.')
if __name__=='__main__':main()
