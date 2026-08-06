-keep class com.argos.companion.** { *; }
-keepclassmembers class com.argos.companion.MainActivity {
    public void onChatResponse(java.lang.String);
    public void onChatError(java.lang.String);
    public void onChatStream(java.lang.String);
}
