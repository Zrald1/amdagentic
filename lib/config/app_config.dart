/// Central configuration for Aria.
///
/// The GPU server runs llama-server (OpenAI-compatible API) on the AMD Radeon
/// GPU. The desktop/phone client connects to it over HTTP. Override the URL
/// at compile time with --dart-define=GPU_SERVER_URL=http://192.168.1.50:8080
class AppConfig {
  const AppConfig._();

  /// Base URL of the OpenAI-compatible inference server (llama-server).
  static const String gpuServerUrl = String.fromEnvironment(
    'GPU_SERVER_URL',
    defaultValue: 'http://localhost:8080',
  );

  /// Model identifier to send to the server.
  static const String model = String.fromEnvironment(
    'MODEL',
    defaultValue: 'qwen2.5-7b-instruct',
  );

  /// How mascot behaves — random roam range in pixels from screen edges.
  static const double mascotSize = 120.0;
  static const double mascotBottomPadding = 8.0;
  static const Duration idleMin = Duration(seconds: 4);
  static const Duration idleMax = Duration(seconds: 12);
  static const double walkSpeed = 80.0; // px per second
}
