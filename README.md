# Xtended Runtime Detection

A lightweight Windows tray application that monitors your clipboard for suspicious content, prompts you to **Discard** or **Keep**, and writes a full audit trail to a log file. Perfect for security-conscious environments where paste-based attacks (e.g. stealthy PowerShell one-liners - ClickFix) are a concern.


## Key Features

- **Pattern-based detection**  
  Load user-defined regular expressions from `patterns.txt` and scan every Unicode text snippet for indicators of malicious payloads (e.g. `powershell.exe -NoP -NonI -W Hidden -Exec Bypass …`).

- **Seamless UI integration**  
  - Uses native TaskDialog prompts for “Discard?” vs “Paste now…” workflows.  
  - Keyboard (`WH_KEYBOARD_LL`) and mouse (`WH_MOUSE_LL`) hooks ensure only one authorized paste proceeds, and block any other input during the decision.

- **Atomic, thread-safe logging**  
  - Captures `Time`, `User`, `Host`, `SourceApp`, `DestApp`, `Content` snippet and `Action` (“Keep”/“Discard”).  


- **Single-instance guard & robust error handling**  
  Prevents multiple instances via a named mutex and surfaces initialization failures with a MessageBox.


##  How It Works

1. **Initialization**  
   - Load regex patterns from `patterns.txt` (skip comments / blanks).  
   - Create a hidden message-only window to receive `WM_CLIPBOARDUPDATE`.  
   - Cache current **user** and **host** names for later logging.


2. **Clipboard update handler**  
   ```cpp
   void ClipboardWatcher::OnClipboardUpdate()
   {
       if (!OpenClipboard(_hWnd)) return;

       // 1) Grab CF_UNICODETEXT and scan with std::regex_search().
       // 2) (optional) Scan file drops or image data …
       CloseClipboard();

       if (!suspicious) return;

       // Install hooks → show TaskDialog → log user choice.
   }


3. Decision workflow


| Action  | Behaviour                                                                                                   |
|---------|-------------------------------------------------------------------------------------------------------------|
| Discard | Clears the clipboard, shows a red **“Content discarded”** toast, and logs **Action: Discard**.              |
| Keep    | Prompts **“Paste now (Ctrl+V / Shift+Ins / right-click)”**, allows one paste, and logs **Action: Keep**. |



Security-Message:


![Security-Alert](https://github.com/user-attachments/assets/69086824-1007-4f43-952a-edf7134bf1a8)



Discarded:


![Clipboard Verdict - Discarded](https://github.com/user-attachments/assets/c02f850f-7ea9-442b-b745-82736d79e086)


Kept:


![Clipboard Verdict](https://github.com/user-attachments/assets/cd6e46e8-91db-4b74-b96f-22d94318173b)




4. Detailed audit trail


![Log](https://github.com/user-attachments/assets/c5d50388-c929-4693-b910-99ce638f285a)


![Log 2](https://github.com/user-attachments/assets/151051e9-231d-4339-856f-f9fba0273edc)


## Getting Started


```powershell
git clone https://github.com/your-org/xtended-runtime-detection.git
cd xtended-runtime-detection
```


- Build requirements: Visual Studio 2019 + with C++17, link against user32, comctl32, psapi, Shlwapi.


Configure Patterns
Edit patterns.txt – one regex per line (# for comments).

Run the Tray App
Double-click xTended Runtime Detection.exe → tray icon appears.

Right-click the icon for Open Logs / Exit.

![Tray](https://github.com/user-attachments/assets/87c82a44-98b3-400e-8529-d79da02b8dc1)



## Roadmap
- Enable CF_HDROP & CF_DIB scanning (file drops, steganography).
- Combine patterns into a single mega-regex for higher throughput.
- Cross-platform support (macOS/Linux clipboard APIs).


---
