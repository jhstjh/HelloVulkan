@echo off

@echo Creating HelloVulkan Android VC14
cmake.exe -H. -Bbuild\aarch64-android -G "Visual Studio 14" -DHV_TARGET=aarch64-android %*
if %errorlevel% neq 0 set ERROR=1

@echo Creating HelloVulkan VC14
cmake.exe -H. -Bbuild\x64-windows -G "Visual Studio 14 Win64" -DHV_TARGET=x64-windows %*
if %errorlevel% neq 0 set ERROR=

@echo Create Projects complete