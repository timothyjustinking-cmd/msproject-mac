@echo off
rem Launch the Claude Code CLI against DeepSeek's Anthropic-compatible endpoint.
rem Per-process only: nothing here touches the user's global environment or the
rem Claude desktop app.  Key: DEEPSEEK_API_KEY, or the first line of
rem %LOCALAPPDATA%\deepseek\api_key.txt.  Usage: tools\deepseek\deepseek-claude.cmd [claude args]
setlocal
set "KEYFILE=%LOCALAPPDATA%\deepseek\api_key.txt"
if "%DEEPSEEK_API_KEY%"=="" if exist "%KEYFILE%" set /p DEEPSEEK_API_KEY=<"%KEYFILE%"
if "%DEEPSEEK_API_KEY%"=="" (
  echo No DeepSeek key: set DEEPSEEK_API_KEY or put the key in "%KEYFILE%".
  exit /b 1
)
set "ANTHROPIC_BASE_URL=https://api.deepseek.com/anthropic"
set "ANTHROPIC_AUTH_TOKEN=%DEEPSEEK_API_KEY%"
set "ANTHROPIC_API_KEY="
set "ANTHROPIC_MODEL=deepseek-flash[1m]"
set "ANTHROPIC_DEFAULT_OPUS_MODEL=deepseek-flash[1m]"
set "ANTHROPIC_DEFAULT_SONNET_MODEL=deepseek-flash[1m]"
set "ANTHROPIC_DEFAULT_HAIKU_MODEL=deepseek-flash"
set "CLAUDE_CODE_SUBAGENT_MODEL=deepseek-flash"
set "CLAUDE_CODE_EFFORT_LEVEL=max"
set "CLAUDE_CODE_AUTO_COMPACT_WINDOW=786432"
cd /d "%~dp0..\.."
claude %*
