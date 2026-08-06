#!/usr/bin/env python3
"""
Simulates a DeepSeek SSE streaming response to verify our C++ parsing logic.
Sends a request to the actual API and checks the response format.
"""

import urllib.request
import json
import ssl
import sys

URL = "https://developer.amd.com.cn/radeon/api/v1/chat/completions"
API_KEY = "rc-c042ad0acc56669f7b46e70f924189b5ac51664ce329f5b2"
MODEL = "DeepSeek-V4-Flash"

body = json.dumps({
    "model": MODEL,
    "stream": True,
    "messages": [
        {"role": "user", "content": "What is 15*17? Think step by step."}
    ]
}).encode('utf-8')

req = urllib.request.Request(URL, data=body, method='POST')
req.add_header('Content-Type', 'application/json')
req.add_header('Accept', 'text/event-stream')
req.add_header('Authorization', f'Bearer {API_KEY}')

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

print(f"Connecting to {URL}...")
try:
    resp = urllib.request.urlopen(req, context=ctx, timeout=30)
    print(f"Status: {resp.status}")
    print(f"Content-Type: {resp.headers.get('Content-Type')}")
    print()

    full_content = ""
    full_thoughts = ""
    chunk_count = 0
    has_reasoning = False

    for line in resp:
        line = line.decode('utf-8').strip()
        if not line:
            continue
        if line.startswith('data:'):
            data = line[5:].strip()
            if data == '[DONE]':
                print("\n[DONE] received")
                break
            chunk_count += 1
            try:
                obj = json.loads(data)
                choices = obj.get('choices', [])
                if choices:
                    delta = choices[0].get('delta', {})
                    reasoning = delta.get('reasoning_content')
                    content = delta.get('content')
                    if reasoning:
                        has_reasoning = True
                        full_thoughts += reasoning
                    if content:
                        full_content += content
            except json.JSONDecodeError as e:
                print(f"  JSON parse error: {e}")

    print(f"\n=== Results ===")
    print(f"Total SSE chunks: {chunk_count}")
    print(f"Has reasoning_content: {has_reasoning}")
    print(f"Thoughts length: {len(full_thoughts)}")
    print(f"Content length: {len(full_content)}")
    print(f"\nThoughts: {full_thoughts[:200]}...")
    print(f"\nContent: {full_content}")

    if has_reasoning:
        print("\n✅ reasoning_content field detected — thoughts display will work")
    else:
        print("\n⚠️ No reasoning_content in this response (model may not support it for this query)")

    if full_content:
        print("✅ Content extracted — chat response will display correctly")
    else:
        print("❌ No content extracted — parsing issue")

except Exception as e:
    print(f"❌ Error: {e}")
    sys.exit(1)
