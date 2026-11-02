@echo off
rem peg2arm guided installer: double-click this file.
title peg2arm installer
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
if errorlevel 1 pause
