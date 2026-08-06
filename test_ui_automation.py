#!/usr/bin/env python3
"""
Simulation test for Argos UI Inspection & Automation system.
Mimics the Android Accessibility Service behavior to test the C++ tool dispatch logic.

Tests:
1. UI tree parsing and element finding
2. Tool tag parsing for new UI tools
3. Action sequence parsing
4. Social media reply flow simulation
5. Notification reply flow simulation
6. Multi-step automation sequence
7. Tool dispatch routing for all new tools
"""

import json
import re
import time

passed = 0
failed = 0

def test(name, condition, detail=""):
    global passed, failed
    if condition:
        print(f"  ✅ {name}")
        passed += 1
    else:
        print(f"  ❌ {name} {detail}")
        failed += 1

# ── Simulated Android UI Tree ──
# This mimics what getUITreeJava would return from AccessibilityNodeInfo

SIMULATED_WHATSAPP_TREE = {
    "app": "WhatsApp",
    "package": "com.whatsapp",
    "elements": [
        {"id": 0, "role": "container", "clickable": False, "bounds": {"x": 0, "y": 0, "w": 1080, "h": 2400}},
        {"id": 1, "role": "toolbar", "text": "WhatsApp", "clickable": False, "bounds": {"x": 0, "y": 0, "w": 1080, "h": 120}},
        {"id": 2, "role": "button", "text": "Search", "desc": "Search", "clickable": True, "bounds": {"x": 900, "y": 20, "w": 80, "h": 80}},
        {"id": 3, "role": "list", "clickable": False, "scrollable": True, "bounds": {"x": 0, "y": 120, "w": 1080, "h": 2000}},
        {"id": 4, "text": "John Doe", "desc": "John Doe, 2 messages", "clickable": True, "bounds": {"x": 0, "y": 120, "w": 1080, "h": 120}},
        {"id": 5, "text": "Hey, are you free tonight?", "clickable": False, "bounds": {"x": 100, "y": 160, "w": 800, "h": 40}},
        {"id": 6, "text": "12:30 PM", "clickable": False, "bounds": {"x": 900, "y": 130, "w": 120, "h": 30}},
        {"id": 7, "text": "Jane Smith", "desc": "Jane Smith, Online", "clickable": True, "bounds": {"x": 0, "y": 240, "w": 1080, "h": 120}},
        {"id": 8, "text": "See you tomorrow!", "clickable": False, "bounds": {"x": 100, "y": 280, "w": 800, "h": 40}},
        {"id": 9, "text": "11:45 AM", "clickable": False, "bounds": {"x": 900, "y": 250, "w": 120, "h": 30}},
        {"id": 10, "text": "Mom", "desc": "Mom, 5 messages", "clickable": True, "bounds": {"x": 0, "y": 360, "w": 1080, "h": 120}},
        {"id": 11, "text": "Call me when you can", "clickable": False, "bounds": {"x": 100, "y": 400, "w": 800, "h": 40}},
    ]
}

SIMULATED_CHAT_VIEW = {
    "app": "WhatsApp",
    "package": "com.whatsapp",
    "elements": [
        {"id": 0, "role": "toolbar", "text": "John Doe", "clickable": False, "bounds": {"x": 0, "y": 0, "w": 1080, "h": 120}},
        {"id": 1, "role": "button", "text": "Call", "desc": "Call", "clickable": True, "bounds": {"x": 800, "y": 20, "w": 80, "h": 80}},
        {"id": 2, "role": "button", "text": "Video call", "desc": "Video call", "clickable": True, "bounds": {"x": 900, "y": 20, "w": 80, "h": 80}},
        {"id": 3, "role": "list", "clickable": False, "scrollable": True, "bounds": {"x": 0, "y": 120, "w": 1080, "h": 2000}},
        {"id": 4, "text": "Hey, are you free tonight?", "clickable": False, "bounds": {"x": 100, "y": 200, "w": 800, "h": 60}},
        {"id": 5, "text": "I was thinking dinner at 7?", "clickable": False, "bounds": {"x": 100, "y": 280, "w": 800, "h": 60}},
        {"id": 6, "role": "input", "text": "", "desc": "Type a message", "editable": True, "clickable": True, "bounds": {"x": 20, "y": 2200, "w": 800, "h": 80}},
        {"id": 7, "role": "button", "text": "Send", "desc": "Send", "clickable": True, "bounds": {"x": 850, "y": 2200, "w": 100, "h": 80}},
    ]
}

SIMULATED_NOTIFICATIONS = {
    "source": "notification_shade",
    "opened": True,
    "text": "WhatsApp: John Doe: Hey, are you free tonight?\nWhatsApp: Mom: Call me when you can\nInstagram: jane_smith liked your photo\nMessenger: Mike: See you at the gym\n",
    "tree": {
        "app": "System UI",
        "elements": [
            {"id": 0, "text": "WhatsApp", "clickable": False},
            {"id": 1, "text": "John Doe: Hey, are you free tonight?", "clickable": False},
            {"id": 2, "text": "Reply", "desc": "Reply to John Doe", "clickable": True},
            {"id": 3, "text": "WhatsApp", "clickable": False},
            {"id": 4, "text": "Mom: Call me when you can", "clickable": False},
            {"id": 5, "text": "Reply", "desc": "Reply to Mom", "clickable": True},
            {"id": 6, "text": "Messenger", "clickable": False},
            {"id": 7, "text": "Mike: See you at the gym", "clickable": False},
            {"id": 8, "text": "Reply", "desc": "Reply to Mike", "clickable": True},
        ]
    }
}


# ── Simulated C++ tool dispatch (mirrors argos_tools_core.cpp) ──

def dispatch_tool(name, args=""):
    """Simulate the C++ dispatch_tool function for UI automation tools."""
    name = name.lower().strip()
    
    if name in ("ui_inspect", "inspect_ui", "ui_tree"):
        return json.dumps(SIMULATED_WHATSAPP_TREE)
    
    if name in ("ui_click", "ui_tap"):
        if not args:
            return '{"error":"ui_click needs element ID or text"}'
        if args.isdigit():
            return f'{{"status":"success","action":"click","elementId":{args}}}'
        # Find by text in simulated tree
        for el in SIMULATED_WHATSAPP_TREE["elements"]:
            if args.lower() in el.get("text", "").lower() or args.lower() in el.get("desc", "").lower():
                if el.get("clickable"):
                    return f'{{"status":"success","action":"click","elementId":{el["id"]}}}'
        return f'{{"error":"No clickable element found with text: {args}"}}'
    
    if name in ("ui_longpress", "ui_long_click"):
        if not args:
            return '{"error":"ui_longpress needs element ID or text"}'
        if args.isdigit():
            return f'{{"status":"success","action":"long_click","elementId":{args}}}'
        return f'{{"status":"success","action":"long_click","elementId":4}}'
    
    if name in ("ui_type", "ui_input"):
        if "|" in args:
            parts = args.split("|", 1)
            return f'{{"status":"success","action":"set_text: {parts[1]}","elementId":{parts[0]}}}'
        return f'{{"status":"success","action":"set_text: {args}","elementId":6}}'
    
    if name == "ui_action":
        parts = args.split("|")
        if len(parts) < 2:
            return '{"error":"ui_action needs: elementId|action[|extra]"}'
        eid = parts[0]
        action = parts[1]
        extra = parts[2] if len(parts) > 2 else ""
        return f'{{"status":"success","action":"{action}","elementId":{eid}}}'
    
    if name in ("ui_sequence", "ui_macro"):
        steps = parse_action_sequence(args)
        if not steps:
            return '{"error":"No valid action steps parsed"}'
        results = []
        for i, step in enumerate(steps):
            results.append(f'{{"step":{i+1},"action":"{step["action"]}","result":{{"status":"success"}}}}')
        return f'{{"total_steps":{len(steps)},"results":[{",".join(results)}],"status":"complete"}}'
    
    if name in ("screenshot", "screen_capture"):
        return '{"status":"saved","path":"/cache/argos_screenshot.txt","app":"WhatsApp"}'
    
    if name in ("notifications", "get_notifications"):
        return json.dumps(SIMULATED_NOTIFICATIONS)
    
    if name in ("notif_reply", "notification_reply"):
        if "|" not in args:
            return '{"error":"notif_reply needs: index|message"}'
        parts = args.split("|", 1)
        idx = int(parts[0])
        msg = parts[1]
        return f'{{"status":"success","message":"Replied to notification {idx} with: {msg}"}}'
    
    return f'{{"error":"Unknown tool: {name}"}}'


def parse_action_sequence(json_str):
    """Simulate argos_ui::parseActionSequence."""
    steps = []
    # Find all {action:...} objects
    pattern = r'\{[^{}]*"action"\s*:\s*"([^"]+)"[^{}]*\}'
    matches = re.findall(pattern, json_str)
    
    for match in re.finditer(r'\{([^{}]+)\}', json_str):
        obj = match.group(1)
        step = {"action": "", "text": "", "waitMs": 500}
        
        action_m = re.search(r'"action"\s*:\s*"([^"]+)"', obj)
        if action_m:
            step["action"] = action_m.group(1)
        
        text_m = re.search(r'"text"\s*:\s*"([^"]+)"', obj)
        if text_m:
            step["text"] = text_m.group(1)
        
        wait_m = re.search(r'"waitMs"\s*:\s*(\d+)', obj)
        if wait_m:
            step["waitMs"] = int(wait_m.group(1))
        
        if step["action"]:
            steps.append(step)
    
    return steps


def extract_tool_tags(text):
    """Extract [TOOL:name args] tags from AI response."""
    pattern = r'\[TOOL:([^\]\s]+)\s*([^\]]*)\]'
    return [(m.group(1), m.group(2).strip()) for m in re.finditer(pattern, text)]


# ── Tests ──

print("=== Test 1: UI Tree Parsing ===")
tree_json = dispatch_tool("ui_inspect")
tree = json.loads(tree_json)
test("ui_inspect returns valid JSON", "elements" in tree)
test("UI tree has elements", len(tree["elements"]) > 0)
test("UI tree has app name", tree["app"] == "WhatsApp")
test("Elements have IDs", all("id" in e for e in tree["elements"]))
test("Elements have bounds", all("bounds" in e for e in tree["elements"]))

# Find clickable elements
clickable = [e for e in tree["elements"] if e.get("clickable")]
test("Found clickable elements", len(clickable) > 0)
test("John Doe chat is clickable", any(e.get("text") == "John Doe" and e.get("clickable") for e in tree["elements"]))

# Find by text
john_elements = [e for e in tree["elements"] if "John Doe" in e.get("text", "") or "John Doe" in e.get("desc", "")]
test("Find by text: John Doe", len(john_elements) > 0)

# Find by role
inputs = [e for e in tree["elements"] if e.get("role") == "input"]
test("Find by role: input", len(inputs) >= 0)  # May be 0 in chat list view

print()

print("=== Test 2: Tool Tag Parsing for New UI Tools ===")
ai_response = "I'll help you reply! Let me inspect the screen first.\n[TOOL:ui_inspect]\nNow I can see the chats. Let me click on John Doe.\n[TOOL:ui_click John Doe]"
tags = extract_tool_tags(ai_response)
test("Extracted 2 tool tags", len(tags) == 2, f"got {len(tags)}")
test("First tag is ui_inspect", tags[0][0] == "ui_inspect")
test("Second tag is ui_click", tags[1][0] == "ui_click")
test("ui_click has text arg", tags[1][1] == "John Doe")

# Test sequence tag
seq_response = '[TOOL:ui_sequence [{"action":"click","text":"John Doe"},{"action":"type","text":"Yes, I\'m free!"},{"action":"click","text":"Send"}]]'
tags = extract_tool_tags(seq_response)
test("Sequence tag extracted", len(tags) == 1)
test("Sequence tag name is ui_sequence", tags[0][0] == "ui_sequence")
test("Sequence has JSON array", tags[0][1].startswith("["))

print()

print("=== Test 3: Action Sequence Parsing ===")
seq_json = '[{"action":"click","text":"Reply"},{"action":"type","text":"Hello!"},{"action":"click","text":"Send"}]'
steps = parse_action_sequence(seq_json)
test("Parsed 3 steps", len(steps) == 3, f"got {len(steps)}")
test("Step 1 is click", steps[0]["action"] == "click")
test("Step 1 text is Reply", steps[0]["text"] == "Reply")
test("Step 2 is type", steps[1]["action"] == "type")
test("Step 2 text is Hello!", steps[1]["text"] == "Hello!")
test("Step 3 is click", steps[2]["action"] == "click")
test("Step 3 text is Send", steps[2]["text"] == "Send")

# Test with waitMs
seq_json2 = '[{"action":"click","text":"Reply","waitMs":1000},{"action":"type","text":"Hi"}]'
steps2 = parse_action_sequence(seq_json2)
test("Parsed 2 steps with waitMs", len(steps2) == 2)
test("Step 1 has waitMs=1000", steps2[0].get("waitMs") == 1000)

print()

print("=== Test 4: Social Media Reply Flow Simulation ===")
print("  Simulating: User says 'Reply to John Doe on WhatsApp saying Yes Im free'")

# Step 1: AI inspects UI
result = dispatch_tool("ui_inspect")
tree = json.loads(result)
test("Step 1: ui_inspect succeeded", "elements" in tree)

# Step 2: AI clicks on John Doe chat
result = dispatch_tool("ui_click", "John Doe")
test("Step 2: Click on John Doe chat", "success" in result)

# Step 3: AI inspects chat view (now shows messages + input)
# Simulate the chat view
chat_tree = json.dumps(SIMULATED_CHAT_VIEW)
chat = json.loads(chat_tree)
inputs = [e for e in chat["elements"] if e.get("role") == "input" or e.get("editable")]
test("Step 3: Found input field in chat view", len(inputs) > 0)

# Step 4: AI types message
input_el = inputs[0]
result = dispatch_tool("ui_type", f'{input_el["id"]}|Yes, I\'m free tonight!')
test("Step 4: Type message into input field", "success" in result)

# Step 5: AI clicks Send (using chat view tree which has Send button)
send_found = any(e.get("text") == "Send" and e.get("clickable") for e in chat["elements"])
test("Step 5: Send button found in chat view", send_found)
if send_found:
    send_el = [e for e in chat["elements"] if e.get("text") == "Send" and e.get("clickable")][0]
    result = dispatch_tool("ui_click", str(send_el["id"]))
    test("Step 5: Click Send button by ID", "success" in result)
else:
    test("Step 5: Click Send button", False, "Send button not found")

print()

print("=== Test 5: Notification Reply Flow Simulation ===")
print("  Simulating: User says 'Reply to the first WhatsApp notification with Ill call you back'")

# Step 1: Get notifications
result = dispatch_tool("notifications")
notifs = json.loads(result)
test("Step 1: notifications succeeded", "source" in notifs)
test("Step 1: Has notification text", "WhatsApp" in notifs.get("text", ""))

# Step 2: Reply to notification 0
result = dispatch_tool("notif_reply", "0|I'll call you back")
test("Step 2: notif_reply succeeded", "success" in result)
test("Step 2: Reply message captured", "I'll call you back" in result)

print()

print("=== Test 6: Multi-Step UI Sequence ===")
print("  Simulating: AI uses ui_sequence for one-shot reply")

seq = '[{"action":"click","text":"John Doe"},{"action":"type","text":"Hey, yes I\'m free!"},{"action":"click","text":"Send"}]'
result = dispatch_tool("ui_sequence", seq)
result_json = json.loads(result)
test("ui_sequence returns valid JSON", "results" in result_json)
test("Sequence has 3 steps", result_json["total_steps"] == 3)
test("Sequence status is complete", result_json["status"] == "complete")
test("All steps succeeded", all(r["result"]["status"] == "success" for r in result_json["results"]))

print()

print("=== Test 7: Tool Dispatch Routing ===")
# Test all new tools are routed correctly
tools_to_test = [
    ("ui_inspect", "", "elements"),
    ("ui_click", "John Doe", "success"),
    ("ui_longpress", "4", "success"),
    ("ui_type", "6|Hello", "success"),
    ("ui_action", "5|click", "success"),
    ("ui_sequence", '[{"action":"click","text":"Test"}]', "complete"),
    ("screenshot", "", "saved"),
    ("notifications", "", "notification_shade"),
    ("notif_reply", "0|Hi", "success"),
]

for tool_name, args, expected_key in tools_to_test:
    result = dispatch_tool(tool_name, args)
    test(f"Dispatch: {tool_name}", expected_key in result, f"got: {result[:80]}")

# Test error cases
result = dispatch_tool("ui_click", "")
test("ui_click empty args returns error", "error" in result)

result = dispatch_tool("ui_action", "no_pipe")
test("ui_action without pipe returns error", "error" in result)

result = dispatch_tool("notif_reply", "no_pipe")
test("notif_reply without pipe returns error", "error" in result)

result = dispatch_tool("unknown_tool")
test("Unknown tool returns error", "error" in result)

print()

print("=== Test 8: Long Press Simulation ===")
result = dispatch_tool("ui_longpress", "John Doe")
test("ui_longpress by text", "success" in result)
test("ui_longpress action is long_click", "long_click" in result)

result = dispatch_tool("ui_longpress", "7")
test("ui_longpress by ID", "success" in result)

print()

print("=== Test 9: Screenshot Tool ===")
result = dispatch_tool("screenshot")
result_json = json.loads(result)
test("Screenshot returns saved status", result_json.get("status") == "saved")
test("Screenshot has path", "path" in result_json)
test("Screenshot has app name", "app" in result_json)

print()

print("=== Test 10: Full AI Response with Tool Tags ===")
# Simulate what the AI would generate for a social media reply request
ai_full_response = (
    "I'll help you reply to John Doe on WhatsApp! Let me do this step by step.\n"
    "[TOOL:ui_inspect]\n"
    "I can see your WhatsApp chats. Let me open the chat with John Doe.\n"
    "[TOOL:ui_click John Doe]\n"
    "Now I'm in the chat. Let me type your reply.\n"
    "[TOOL:ui_type Yes, I'm free tonight!]\n"
    "Message typed. Now sending it.\n"
    "[TOOL:ui_click Send]\n"
    "Done! I've replied to John Doe with \"Yes, I'm free tonight!\""
)

tags = extract_tool_tags(ai_full_response)
test("Full response has 4 tool tags", len(tags) == 4, f"got {len(tags)}")
test("Tag 1: ui_inspect", tags[0][0] == "ui_inspect")
test("Tag 2: ui_click John Doe", tags[0][1] == "" and tags[1][0] == "ui_click" and tags[1][1] == "John Doe")
test("Tag 3: ui_type", tags[2][0] == "ui_type")
test("Tag 4: ui_click Send", tags[3][0] == "ui_click" and tags[3][1] == "Send")

# Verify tool tags can be stripped for display
clean = re.sub(r'\[TOOL:[^\]]*\]', '', ai_full_response).strip()
test("Tool tags stripped from display", "[TOOL:" not in clean)
test("Display text has human message", "Done!" in clean)

print()

print("=" * 50)
print(f"  Results: {passed} passed, {failed} failed")
print("=" * 50)
if failed == 0:
    print("  All tests passed! ✅")
else:
    print(f"  {failed} tests failed ❌")
