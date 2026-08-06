#!/usr/bin/env python3
"""
Simulation test for Argos Android integration.
Tests: SSE parsing, reasoning_content extraction, tool tag detection, tool dispatch, config loading.

This mimics what the C++ core does on Android, verifying the logic is correct.
"""

import json
import re
import os
import sys
import time

PASSED = 0
FAILED = 0

def test_pass(name):
    global PASSED
    PASSED += 1
    print(f"  ✅ {name}")

def test_fail(name, reason=""):
    global FAILED
    FAILED += 1
    print(f"  ❌ {name}: {reason}")

# ─── Test 1: SSE chunk parsing with reasoning_content ───
print("\n=== Test 1: SSE Parsing with reasoning_content ===")

def parse_sse_chunk(chunk):
    """Mimics AgentClientCore::parseSSEChunk"""
    result = ""
    thoughts = ""
    lines = chunk.split('\n')
    for line in lines:
        line = line.rstrip('\r')
        if line.startswith('data:'):
            data = line[5:].lstrip()
            if data == '[DONE]' or not data:
                continue
            try:
                obj = json.loads(data)
                choices = obj.get('choices', [])
                if choices:
                    delta = choices[0].get('delta', {})
                    reasoning = delta.get('reasoning_content', None)
                    content = delta.get('content', None)
                    if reasoning and reasoning is not None:
                        thoughts += reasoning
                    if content and content is not None:
                        result += content
            except json.JSONDecodeError:
                pass
    return result, thoughts

# Simulate DeepSeek SSE stream with reasoning_content
sse_chunk1 = """data: {"choices":[{"delta":{"reasoning_content":"Let me think about this...","content":null}}]}

data: {"choices":[{"delta":{"reasoning_content":"The user wants to know about cats.","content":null}}]}

data: {"choices":[{"delta":{"reasoning_content":null,"content":"Cats are"}}]}

data: {"choices":[{"delta":{"reasoning_content":null,"content":" amazing"}}]}

data: {"choices":[{"delta":{"reasoning_content":null,"content":" animals."}}]}

data: [DONE]
"""

content, thoughts = parse_sse_chunk(sse_chunk1)

if thoughts == "Let me think about this...The user wants to know about cats.":
    test_pass("Reasoning content extracted correctly")
else:
    test_fail("Reasoning content extraction", f"Got: '{thoughts}'")

if content == "Cats are amazing animals.":
    test_pass("Content extracted correctly")
else:
    test_fail("Content extraction", f"Got: '{content}'")

# ─── Test 2: Tool tag detection ───
print("\n=== Test 2: Tool Tag Detection ===")

def has_tool_tags(response):
    return '[TOOL:' in response

def extract_tool_tags(response):
    """Extract tool name and args from [TOOL:name args] tags"""
    tags = []
    pos = 0
    while True:
        start = response.find('[TOOL:', pos)
        if start == -1:
            break
        end = response.find(']', start)
        if end == -1:
            break
        tag_content = response[start+6:end]
        parts = tag_content.split(' ', 1)
        name = parts[0]
        args = parts[1] if len(parts) > 1 else ""
        tags.append((name, args))
        pos = end + 1
    return tags

def strip_tool_tags(response):
    return re.sub(r'\[TOOL:[^\]]*\]', '', response)

# Test detection
assert has_tool_tags("Let me check [TOOL:list_files /sdcard]") == True
assert has_tool_tags("Just a normal response") == False
test_pass("Tool tag detection")

# Test extraction
tags = extract_tool_tags("I'll search for that: [TOOL:open_url https://google.com/search?q=cats] done")
assert len(tags) == 1
assert tags[0][0] == "open_url"
assert tags[0][1] == "https://google.com/search?q=cats"
test_pass("Tool tag extraction")

# Test multiple tags
tags = extract_tool_tags("[TOOL:list_files /sdcard] and [TOOL:screen_text]")
assert len(tags) == 2
assert tags[0] == ("list_files", "/sdcard")
assert tags[1] == ("screen_text", "")
test_pass("Multiple tool tag extraction")

# Test stripping
cleaned = strip_tool_tags("Let me check [TOOL:list_files /sdcard] for you")
assert "[TOOL:" not in cleaned
assert "Let me check  for you" == cleaned
test_pass("Tool tag stripping")

# ─── Test 3: Tool dispatch simulation ───
print("\n=== Test 3: Tool Dispatch Simulation ===")

def dispatch_tool(name, args):
    """Simulates argos_tools::dispatch_tool"""
    name = name.lower()
    
    if name in ("list_files", "dir", "ls"):
        return f'{{"files":["file1.txt","file2.jpg","dir1"]}}'
    if name == "read":
        return '{"content":"file contents here"}'
    if name in ("cmd", "command", "shell"):
        return '{"output":"command output"}'
    if name == "recall":
        return '{"memory":"previous conversation"}'
    if name == "forget":
        return '{"status":"memory cleared"}'
    if name in ("open_url", "browser_open", "navigate"):
        return f'{{"status":"Opening URL: {args}"}}'
    if name in ("screen_text", "read_screen", "browser_content"):
        return '{"text":"Screen content here"}'
    if name in ("screen_active", "active_app"):
        return '{"package":"com.android.chrome"}'
    if name in ("click_text", "browser_click"):
        return f'{{"status":"Clicked on text: {args}"}}'
    if name in ("type_text", "browser_type"):
        return f'{{"status":"Typed text into focused field"}}'
    if name == "scroll":
        direction = "up" if args in ("up", "0") else "down"
        return f'{{"status":"Scrolled {direction}"}}'
    if name == "search":
        return '{"error":"Web search not available on mobile platform. Use open_url to open a search page, then screen_text to read it."}'
    if name in ("rag_search", "search_files", "search_filename"):
        return '{"error":"RAG search not available on this platform"}'
    return f'{{"error":"Unknown tool: {name}"}}'

# Test each tool
result = dispatch_tool("list_files", "/sdcard")
assert "files" in result
test_pass("list_files dispatch")

result = dispatch_tool("open_url", "https://google.com")
assert "Opening URL" in result
test_pass("open_url dispatch")

result = dispatch_tool("screen_text", "")
assert "text" in result
test_pass("screen_text dispatch")

result = dispatch_tool("click_text", "Search")
assert "Clicked" in result
test_pass("click_text dispatch")

result = dispatch_tool("type_text", "hello world")
assert "Typed" in result
test_pass("type_text dispatch")

result = dispatch_tool("scroll", "down")
assert "Scrolled down" in result
test_pass("scroll dispatch")

result = dispatch_tool("scroll", "up")
assert "Scrolled up" in result
test_pass("scroll up dispatch")

result = dispatch_tool("unknown_tool", "")
assert "Unknown tool" in result
test_pass("Unknown tool error")

# ─── Test 4: Config file loading ───
print("\n=== Test 4: Config File Loading ===")

config_content = """server_url=https://example.com/api
api_key=test-key-12345
model=TestModel-Pro
"""

# Write test config
test_config_path = "/tmp/argos_test_config.txt"
with open(test_config_path, 'w') as f:
    f.write(config_content)

# Simulate config loading
loaded = {}
with open(test_config_path, 'w') as f:
    f.write(config_content)

with open(test_config_path, 'r') as f:
    for line in f:
        eq = line.find('=')
        if eq == -1:
            continue
        key = line[:eq].strip()
        val = line[eq+1:].strip()
        loaded[key] = val

assert loaded.get("server_url") == "https://example.com/api"
assert loaded.get("api_key") == "test-key-12345"
assert loaded.get("model") == "TestModel-Pro"
test_pass("Config file parsing")

os.remove(test_config_path)

# ─── Test 5: Full chat flow simulation ───
print("\n=== Test 5: Full Chat Flow Simulation ===")

# Simulate the full flow: user message → AI response with tool → tool execution → final response
print("  Simulating: User asks 'Search for cats on Google'")
print("  Step 1: AI responds with tool call...")

ai_response_1 = "I'll search for cats on Google for you. [TOOL:open_url https://www.google.com/search?q=cats]"
assert has_tool_tags(ai_response_1)
test_pass("AI response contains tool tag")

tags = extract_tool_tags(ai_response_1)
assert tags[0][0] == "open_url"
assert "google.com" in tags[0][1]
test_pass("Tool tag parsed correctly")

print("  Step 2: Executing tool: open_url...")
tool_result_1 = dispatch_tool(tags[0][0], tags[0][1])
assert "Opening URL" in tool_result_1
test_pass("Tool executed successfully")

print("  Step 3: AI reads screen content...")
ai_response_2 = "Let me read the search results. [TOOL:screen_text]"
tags2 = extract_tool_tags(ai_response_2)
tool_result_2 = dispatch_tool(tags2[0][0], tags2[0][1])
assert "text" in tool_result_2
test_pass("Screen text tool executed")

print("  Step 4: AI provides final answer...")
final_response = "Based on the search results, cats are popular pets known for their independence and agility."
assert not has_tool_tags(final_response)
test_pass("Final response has no tool tags")

clean_display = strip_tool_tags(ai_response_1)
assert "[TOOL:" not in clean_display
test_pass("Display text cleaned of tool tags")

# ─── Test 6: Metrics calculation ───
print("\n=== Test 6: Metrics Calculation ===")

start_time = time.time()
time.sleep(0.1)  # Simulate 100ms response time
end_time = time.time()
duration_ms = int((end_time - start_time) * 1000)

assert duration_ms >= 90  # Allow some tolerance
test_pass(f"Response time measured: {duration_ms}ms")

response_chars = len(final_response)
metrics_str = f"{duration_ms}ms|{response_chars}chars"
assert "ms" in metrics_str
assert "chars" in metrics_str
test_pass(f"Metrics string formatted: {metrics_str}")

# ─── Summary ───
print(f"\n{'='*50}")
print(f"  Results: {PASSED} passed, {FAILED} failed")
print(f"{'='*50}")

if FAILED > 0:
    sys.exit(1)
else:
    print("  All tests passed! ✅")
    sys.exit(0)
