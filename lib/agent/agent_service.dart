import 'dart:async';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../mascot/mascot_controller.dart';
import 'agent_bridge.dart';

/// High-level agent service that orchestrates a task: sends the user prompt
/// to the GPU server, streams the response, and updates the mascot state.
///
/// This is the thin-client version — all inference happens on the remote
/// AMD Radeon GPU server. Tool calling can be added here later by extending
/// the messages with function definitions and handling tool_calls in the
/// response.
class AgentService {
  AgentService(this._ref, this._bridge);

  final Ref _ref;
  final AgentBridge _bridge;

  static const _systemPrompt = '''You are Aria, a friendly and capable AI companion living on the user's desktop.
You are powered by a local LLM running on an AMD Radeon GPU server.
Be concise, helpful, and warm. When performing research, cite sources.
When you don't know something, say so honestly.''';

  /// Execute a user task with streaming output.
  Future<String> executeTask(String userPrompt) async {
    final controller = _ref.read(mascotControllerProvider.notifier);
    controller.onTaskStarted();

    final messages = [
      ChatMessage(role: 'system', content: _systemPrompt),
      ChatMessage(role: 'user', content: userPrompt),
    ];

    try {
      final fullText = await _bridge.chatStream(
        messages: messages,
        onToken: (delta) {
          // Update mascot with partial text + estimated progress.
          final current = _ref.read(mascotControllerProvider);
          final newText = current.thoughtText + delta;
          // Rough progress estimate based on text length vs max tokens.
          final progress = (newText.length / 800).clamp(0.0, 0.95);
          controller.onWorkProgress(progress, newText);
        },
      );

      controller.onTaskCompleted(fullText);
      return fullText;
    } catch (e) {
      controller.onTaskError(e.toString());
      rethrow;
    }
  }

  /// Quick health check of the GPU server.
  Future<bool> isServerAlive() => _bridge.isServerAlive();
}

/// Providers
final agentBridgeProvider = Provider<AgentBridge>((ref) {
  final bridge = AgentBridge();
  ref.onDispose(bridge.dispose);
  return bridge;
});

final agentServiceProvider = Provider<AgentService>((ref) {
  return AgentService(ref, ref.read(agentBridgeProvider));
});
