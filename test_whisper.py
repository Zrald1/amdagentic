#!/usr/bin/env python3
"""Test whisper model loading and audio recording simulation."""

import ctypes
import ctypes.wintypes as wt
import os
import sys
import time
import struct
import wave

# Try to load whisper directly via the whisper.dll/whisper.h API
# Instead, let's test if the model file exists and is valid

model_paths = [
    r"C:\Users\geral\amdagentic\cpp\build\Debug\ggml-tiny.en.bin",
    r"C:\Users\geral\amdagentic\ggml-tiny.en.bin",
    r"C:\Users\geral\ggml-tiny.en.bin",
]

print("=== Whisper Model Check ===")
for p in model_paths:
    if os.path.exists(p):
        size = os.path.getsize(p)
        print(f"  FOUND: {p} ({size / 1024 / 1024:.1f} MB)")
        # Check if it's a valid GGML file (magic number)
        with open(p, 'rb') as f:
            magic = f.read(4)
            print(f"  Magic bytes: {magic.hex()} ({magic})")
            if magic == b'ggml':
                print("  -> Valid GGML format (whisper v1)")
            elif magic == b'ggjt':
                print("  -> Valid GGJT format (whisper v2)")
            elif magic == b'tfwt':
                print("  -> Valid TFWT format")
            else:
                # Try reading as little-endian uint32
                magic_val = struct.unpack('<I', magic)[0]
                print(f"  -> Magic as uint32: {magic_val} (0x{magic_val:08x})")
                if magic_val == 0x67676d6c:  # 'ggml' in little-endian
                    print("  -> Valid GGML format (little-endian)")
                else:
                    print("  -> WARNING: Unknown format!")
    else:
        print(f"  NOT FOUND: {p}")

print()
print("=== Audio Recording Test (Win32 WaveIn) ===")

try:
    winmm = ctypes.windll.winmm
    
    WAVE_FORMAT_PCM = 1
    WAVE_MAPPER = -1
    MMSYSERR_NOERROR = 0
    WHDR_DONE = 0x00000001
    
    class WAVEFORMATEX(ctypes.Structure):
        _fields_ = [
            ("wFormatTag", ctypes.c_ushort),
            ("nChannels", ctypes.c_ushort),
            ("nSamplesPerSec", ctypes.c_uint),
            ("nAvgBytesPerSec", ctypes.c_uint),
            ("nBlockAlign", ctypes.c_ushort),
            ("wBitsPerSample", ctypes.c_ushort),
            ("cbSize", ctypes.c_ushort),
        ]
    
    class WAVEHDR(ctypes.Structure):
        _fields_ = [
            ("lpData", ctypes.c_char_p),
            ("dwBufferLength", ctypes.c_uint),
            ("dwBytesRecorded", ctypes.c_uint),
            ("dwUser", ctypes.c_ulong),
            ("dwFlags", ctypes.c_uint),
            ("dwLoops", ctypes.c_uint),
            ("lpNext", ctypes.c_void_p),
            ("reserved", ctypes.c_ulong),
        ]
    
    sampleRate = 16000
    channels = 1
    bitsPerSample = 16
    duration = 3  # 3 seconds
    totalSamples = duration * sampleRate
    bufferSize = totalSamples * (bitsPerSample // 8) * channels
    
    wfx = WAVEFORMATEX()
    wfx.wFormatTag = WAVE_FORMAT_PCM
    wfx.nChannels = channels
    wfx.nSamplesPerSec = sampleRate
    wfx.nAvgBytesPerSec = sampleRate * channels * (bitsPerSample // 8)
    wfx.nBlockAlign = channels * (bitsPerSample // 8)
    wfx.wBitsPerSample = bitsPerSample
    wfx.cbSize = 0
    
    hWaveIn = ctypes.c_void_p()
    
    result = winmm.waveInOpen(ctypes.byref(hWaveIn), WAVE_MAPPER, 
                              ctypes.byref(wfx), 0, 0, 0)
    print(f"  waveInOpen result: {result} (0 = success)")
    
    if result != MMSYSERR_NOERROR:
        print("  ERROR: Cannot open audio device!")
        print("  -> Check if microphone is available and not in use by another app")
        sys.exit(1)
    
    # Allocate buffer
    buf = (ctypes.c_char * bufferSize)()
    
    whdr = WAVEHDR()
    whdr.lpData = ctypes.cast(buf, ctypes.c_char_p)
    whdr.dwBufferLength = bufferSize
    whdr.dwBytesRecorded = 0
    whdr.dwFlags = 0
    whdr.dwLoops = 0
    
    result = winmm.waveInPrepareHeader(hWaveIn, ctypes.byref(whdr), ctypes.sizeof(WAVEHDR))
    print(f"  waveInPrepareHeader result: {result}")
    
    result = winmm.waveInAddBuffer(hWaveIn, ctypes.byref(whdr), ctypes.sizeof(WAVEHDR))
    print(f"  waveInAddBuffer result: {result}")
    
    result = winmm.waveInStart(hWaveIn)
    print(f"  waveInStart result: {result}")
    
    print(f"  Recording {duration}s... Please speak into the microphone!")
    
    # Wait for recording
    for i in range(duration * 20):
        if whdr.dwFlags & WHDR_DONE:
            print(f"  Buffer done after {i*50}ms")
            break
        time.sleep(0.05)
    
    winmm.waveInStop(hWaveIn)
    
    recordedBytes = whdr.dwBytesRecorded
    print(f"  dwBytesRecorded: {recordedBytes} (expected: {bufferSize})")
    print(f"  dwFlags: 0x{whdr.dwFlags:08x} (WHDR_DONE=0x01)")
    
    if recordedBytes == 0:
        print("  WARNING: dwBytesRecorded is 0! This is the bug!")
        print("  -> The buffer was never filled. Trying with smaller buffer...")
        
        # Try with a smaller buffer (0.5 second)
        smallSize = 8000  # 0.5 sec at 16kHz, 16-bit
        buf2 = (ctypes.c_char * smallSize)()
        whdr2 = WAVEHDR()
        whdr2.lpData = ctypes.cast(buf2, ctypes.c_char_p)
        whdr2.dwBufferLength = smallSize
        whdr2.dwFlags = 0
        
        winmm.waveInPrepareHeader(hWaveIn, ctypes.byref(whdr2), ctypes.sizeof(WAVEHDR))
        winmm.waveInAddBuffer(hWaveIn, ctypes.byref(whdr2), ctypes.sizeof(WAVEHDR))
        winmm.waveInStart(hWaveIn)
        
        print("  Recording 0.5s with small buffer...")
        for i in range(200):
            if whdr2.dwFlags & WHDR_DONE:
                print(f"  Small buffer done after {i*50}ms")
                break
            time.sleep(0.05)
        
        winmm.waveInStop(hWaveIn)
        print(f"  Small buffer dwBytesRecorded: {whdr2.dwBytesRecorded}")
        
        if whdr2.dwBytesRecorded > 0:
            # Save to WAV for inspection
            wavPath = "test_recording.wav"
            with wave.open(wavPath, 'w') as wf:
                wf.setnchannels(channels)
                wf.setsampwidth(bitsPerSample // 8)
                wf.setframerate(sampleRate)
                wf.writeframes(buf2[:whdr2.dwBytesRecorded])
            print(f"  Saved test recording to {wavPath}")
        
        winmm.waveInUnprepareHeader(hWaveIn, ctypes.byref(whdr2), ctypes.sizeof(WAVEHDR))
    else:
        # Save to WAV
        wavPath = "test_recording.wav"
        with wave.open(wavPath, 'w') as wf:
            wf.setnchannels(channels)
            wf.setsampwidth(bitsPerSample // 8)
            wf.setframerate(sampleRate)
            wf.writeframes(buf[:recordedBytes])
        print(f"  Saved test recording to {wavPath} ({recordedBytes} bytes)")
    
    winmm.waveInUnprepareHeader(hWaveIn, ctypes.byref(whdr), ctypes.sizeof(WAVEHDR))
    winmm.waveInClose(hWaveIn)
    
    print()
    print("=== Summary ===")
    if recordedBytes > 0:
        print("  Audio recording works! The issue is likely in whisper initialization.")
    else:
        print("  Audio recording has issues - dwBytesRecorded is 0")
        print("  This could be a microphone permission or device issue")
    
    # Check what devices are available
    numDevs = winmm.waveInGetNumDevs()
    print(f"  Number of wave input devices: {numDevs}")
    
    class WAVEINCAPS(ctypes.Structure):
        _fields_ = [
            ("wMid", ctypes.c_ushort),
            ("wPid", ctypes.c_ushort),
            ("vDriverVersion", ctypes.c_uint),
            ("szPname", ctypes.c_char * 32),
            ("dwFormats", ctypes.c_uint),
            ("wChannels", ctypes.c_ushort),
            ("wReserved1", ctypes.c_ushort),
        ]
    
    for i in range(numDevs):
        caps = WAVEINCAPS()
        winmm.waveInGetDevCapsW(i, ctypes.byref(caps), ctypes.sizeof(WAVEINCAPS))
        name = caps.szPname.decode('utf-8', errors='replace').rstrip('\x00')
        print(f"  Device {i}: {name} (channels={caps.wChannels})")

except Exception as e:
    print(f"  ERROR: {e}")
    import traceback
    traceback.print_exc()
