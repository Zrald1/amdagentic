import 'dart:async';
import 'dart:convert';
import 'package:http/http.dart' as http;

import '../config/app_config.dart';

/// OpenAI-compatible chat completion message.
class ChatMessage {
  ChatMessage({required this.role, required this.content});
  final String role; // "system", "user", "assistant"
  final String content;

  Map<String, dynamic> toJson() => {'role': role, 'content': content};
}

/// Result of a chat completion call.
class ChatResult {
  ChatResult({required this.text, this.tokensPerSecond});
  final String text;
  final double? tokensPerSecond;
}

/// HTTP client that talks to the GPU server's OpenAI-compatible API
/// (llama-server). Supports both blocking and streaming (SSE) completions.
class AgentBridge {
  AgentBridge({String? baseUrl}) : _baseUrl = baseUrl ?? AppConfig.gpuServerUrl;

  final String _baseUrl;
  final http.Client _client = http.Client();

  String get baseUrl => _baseUrl;

  /// Non-streaming chat completion.
  Future<ChatResult> chat({
    required List<ChatMessage> messages,
    double temperature = 0.7,
    int maxTokens = 2048,
  }) async {
    final uri = Uri.parse('$_baseUrl/v1/chat/completions');
    final res = await _client.post(
      uri,
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({
        'model': AppConfig.model,
        'messages': messages.map((m) => m.toJson()).toList(),
        'temperature': temperature,
        'max_tokens': maxTokens,
        'stream': false,
      }),
    );

    if (res.statusCode != 200) {
      throw Exception('Chat API error ${res.statusCode}: ${res.body}');
    }

    final json = jsonDecode(res.body) as Map<String, dynamic>;
    final choices = json['choices'] as List;
    final text = choices[0]['message']['content'] as String;
    final usage = json['usage'] as Map<String, dynamic>?;
    double? tps;
    if (usage != null) {
      final total = usage['total_tokens'] as int?;
      final time = json['timings']?['predicted_ms'] as num?;
      if (total != null && time != null && time > 0) {
        tps = total / (time / 1000);
      }
    }
    return ChatResult(text: text, tokensPerSecond: tps);
  }

  /// Streaming chat completion — calls [onToken] for each delta and
  /// [onDone] when complete. Returns the full text.
  Future<String> chatStream({
    required List<ChatMessage> messages,
    required void Function(String delta) onToken,
    double temperature = 0.7,
    int maxTokens = 2048,
  }) async {
    final uri = Uri.parse('$_baseUrl/v1/chat/completions');
    final req = http.Request('POST', uri)
      ..headers['Content-Type'] = 'application/json'
      ..body = jsonEncode({
        'model': AppConfig.model,
        'messages': messages.map((m) => m.toJson()).toList(),
        'temperature': temperature,
        'max_tokens': maxTokens,
        'stream': true,
      });

    final response = await _client.send(req);
    if (response.statusCode != 200) {
      final body = await response.stream.bytesToString();
      throw Exception('Chat API error ${response.statusCode}: $body');
    }

    final fullText = StringBuffer();
    await for (final chunk in response.stream
        .transform(utf8.decoder)
        .transform(const LineSplitter())) {
      if (!chunk.startsWith('data: ')) continue;
      final data = chunk.substring(6);
      if (data == '[DONE]') break;
      try {
        final json = jsonDecode(data) as Map<String, dynamic>;
        final choices = json['choices'] as List;
        if (choices.isEmpty) continue;
        final delta = choices[0]['delta']?['content'] as String?;
        if (delta != null && delta.isNotEmpty) {
          fullText.write(delta);
          onToken(delta);
        }
      } catch (_) {
        // Skip malformed lines.
      }
    }
    return fullText.toString();
  }

  /// Health check — verify the GPU server is reachable.
  Future<bool> isServerAlive() async {
    try {
      final res = await _client
          .get(Uri.parse('$_baseUrl/health'))
          .timeout(const Duration(seconds: 3));
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  /// Get model info from the server.
  Future<Map<String, dynamic>?> getModels() async {
    try {
      final res = await _client.get(Uri.parse('$_baseUrl/v1/models'));
      if (res.statusCode != 200) return null;
      return jsonDecode(res.body) as Map<String, dynamic>;
    } catch (_) {
      return null;
    }
  }

  void dispose() {
    _client.close();
  }
}
