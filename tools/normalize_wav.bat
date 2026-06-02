@echo off
set /p FOLDER="WAVフォルダのパスを入力してください: "
python "%~dp0normalize_wav.py" "%FOLDER%"
pause
