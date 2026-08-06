#!/usr/bin/env python3
"""Test the thought bubble API call — verifies DeepSeek returns short comments suitable for the bubble."""

import urllib.request
import json
import ssl

URL = "https://developer.amd.com.cn/radeon/api/v1/chat/completions"
API_KEY = "rc-c042ad0acc56669f7b46e70f924189b5ac51664ce329f5b2"
MODEL = "DeepSeek-V4-Flash"

apps = ["Chrome", "YouTube", "Gmail", "the home screen", "WhatsApp"]

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

print("=== Thought Bubble API Test ===\n")

for app in apps:
    prompt = (f"You are Argos, a cute robot companion floating on the user's screen. "
              f"The user just opened {app}. "
              f"Say something brief, funny, or helpful about it (max 15 words, 1 sentence). "
              f"Be casual and friendly. Don't use emojis.")
    
    body = json.dumps({
        "model": MODEL,
        "stream": False,
        "messages": [{"role": "user", "content": prompt}]
    }).encode('utf-8')
    
    req = urllib.request.Request(URL, data=body, method='POST')
    req.add_header('Content-Type', 'application/json')
    req.add_header('Authorization', f'Bearer {API_KEY}')
    
    try:
        resp = urllib.request.urlopen(req, context=ctx, timeout=10)
        data = json.loads(resp.read().decode('utf-8'))
        content = data['choices'][0]['message']['content']
        word_count = len(content.split())
        status = "✅" if word_count <= 20 else "⚠️ too long"
        print(f"  {app:20s} → \"{content}\" ({word_count} words) {status}")
    except Exception as e:
        print(f"  {app:20s} → ❌ Error: {e}")

print("\n=== Done ===")
