import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../agent/agent_service.dart';
import '../mascot/mascot_controller.dart';
import '../mascot/mascot_stage.dart';
import '../mascot/mascot_state.dart';
import 'engine_panel.dart';

/// Overlay that shows the prompt panel when the mascot is clicked, plus the
/// thought bubble during work, and the result panel when done.
class PromptOverlay extends ConsumerStatefulWidget {
  const PromptOverlay({super.key});

  @override
  ConsumerState<PromptOverlay> createState() => _PromptOverlayState();
}

class _PromptOverlayState extends ConsumerState<PromptOverlay> {
  final _controller = TextEditingController();
  final _scrollController = ScrollController();
  String _resultText = '';
  String _errorText = '';
  bool _isWorking = false;
  bool _showEngine = false;

  @override
  void dispose() {
    _controller.dispose();
    _scrollController.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    final prompt = _controller.text.trim();
    if (prompt.isEmpty || _isWorking) return;

    setState(() {
      _isWorking = true;
      _resultText = '';
      _errorText = '';
    });

    try {
      final result = await ref.read(agentServiceProvider).executeTask(prompt);
      setState(() {
        _resultText = result;
        _isWorking = false;
      });
    } catch (e) {
      setState(() {
        _errorText = e.toString();
        _isWorking = false;
      });
    }
  }

  void _close() {
    ref.read(promptVisibleProvider.notifier).hide();
    setState(() {
      _resultText = '';
      _errorText = '';
      _controller.clear();
    });
  }

  @override
  Widget build(BuildContext context) {
    final visible = ref.watch(promptVisibleProvider);
    final mascotStatus = ref.watch(mascotControllerProvider);

    if (!visible && mascotStatus.state != MascotState.working &&
        mascotStatus.state != MascotState.celebrating) {
      return const SizedBox.shrink();
    }

    return Positioned(
      left: 0,
      bottom: 0,
      child: Material(
        color: Colors.transparent,
        child: Container(
          width: 380,
          margin: const EdgeInsets.fromLTRB(16, 0, 16, 140),
          decoration: BoxDecoration(
            color: const Color(0xFF1E1E2E).withValues(alpha: 0.95),
            borderRadius: BorderRadius.circular(20),
            border: Border.all(
              color: const Color(0xFF6C5CE7).withValues(alpha: 0.5),
              width: 1.5,
            ),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withValues(alpha: 0.4),
                blurRadius: 20,
                offset: const Offset(0, 8),
              ),
            ],
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Header
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 12, 8, 4),
                child: Row(
                  children: [
                    Container(
                      width: 10,
                      height: 10,
                      decoration: const BoxDecoration(
                        color: Color(0xFF00CEC9),
                        shape: BoxShape.circle,
                      ),
                    ),
                    const SizedBox(width: 8),
                    const Text(
                      'Aria',
                      style: TextStyle(
                        color: Colors.white,
                        fontSize: 16,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const Spacer(),
                    IconButton(
                      icon: const Icon(Icons.memory, color: Color(0xFFA29BFE), size: 18),
                      onPressed: () => setState(() => _showEngine = !_showEngine),
                      tooltip: 'Engine info',
                    ),
                    IconButton(
                      icon: const Icon(Icons.close, color: Colors.white54, size: 18),
                      onPressed: _close,
                    ),
                  ],
                ),
              ),

              if (_showEngine) ...[
                const Padding(
                  padding: EdgeInsets.symmetric(horizontal: 12),
                  child: EnginePanel(),
                ),
                const SizedBox(height: 8),
              ],

              // Thought / result display
              if (mascotStatus.state == MascotState.working ||
                  _resultText.isNotEmpty ||
                  _errorText.isNotEmpty)
                ConstrainedBox(
                  constraints: const BoxConstraints(maxHeight: 200),
                  child: Container(
                    margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: Colors.white.withValues(alpha: 0.05),
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: SingleChildScrollView(
                      controller: _scrollController,
                      child: _buildContent(mascotStatus),
                    ),
                  ),
                ),

              // Input field
              if (!_isWorking && mascotStatus.state != MascotState.working)
                Padding(
                  padding: const EdgeInsets.fromLTRB(12, 8, 12, 12),
                  child: Row(
                    children: [
                      Expanded(
                        child: TextField(
                          controller: _controller,
                          style: const TextStyle(color: Colors.white, fontSize: 14),
                          decoration: InputDecoration(
                            hintText: 'Ask Aria to do something...',
                            hintStyle: TextStyle(color: Colors.white.withValues(alpha: 0.3)),
                            filled: true,
                            fillColor: Colors.white.withValues(alpha: 0.08),
                            border: OutlineInputBorder(
                              borderRadius: BorderRadius.circular(12),
                              borderSide: BorderSide.none,
                            ),
                            contentPadding: const EdgeInsets.symmetric(
                              horizontal: 14,
                              vertical: 10,
                            ),
                          ),
                          onSubmitted: (_) => _submit(),
                        ),
                      ),
                      const SizedBox(width: 8),
                      IconButton.filled(
                        onPressed: _submit,
                        icon: const Icon(Icons.send, size: 18),
                        style: IconButton.styleFrom(
                          backgroundColor: const Color(0xFF6C5CE7),
                        ),
                      ),
                    ],
                  ),
                ),

              if (_isWorking || mascotStatus.state == MascotState.working)
                const Padding(
                  padding: EdgeInsets.fromLTRB(12, 8, 12, 12),
                  child: Row(
                    children: [
                      SizedBox(
                        width: 16,
                        height: 16,
                        child: CircularProgressIndicator(
                          strokeWidth: 2,
                          color: Color(0xFF00CEC9),
                        ),
                      ),
                      SizedBox(width: 12),
                      Text(
                        'Working on it...',
                        style: TextStyle(color: Colors.white54, fontSize: 13),
                      ),
                    ],
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildContent(MascotStatus status) {
    if (_errorText.isNotEmpty) {
      return Text(
        _errorText,
        style: const TextStyle(color: Colors.redAccent, fontSize: 13),
      );
    }
    final text = _resultText.isNotEmpty ? _resultText : status.thoughtText;
    if (text.isEmpty) return const SizedBox.shrink();
    return Text(
      text,
      style: const TextStyle(color: Colors.white70, fontSize: 13, height: 1.4),
    );
  }
}
