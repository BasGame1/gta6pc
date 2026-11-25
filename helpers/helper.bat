@echo off
setlocal EnableDelayedExpansion

for /F %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
set "GRAD_R_0=255" & set "GRAD_G_0=195" & set "GRAD_B_0=113"
set "GRAD_R_1=242" & set "GRAD_G_1=92"  & set "GRAD_B_1=162"
set "GRAD_R_2=145" & set "GRAD_G_2=85"  & set "GRAD_B_2=245"
set "GRAD_R_3=20"  & set "GRAD_G_3=120" & set "GRAD_B_3=255"
set "FUNC=%~1"
set "MSG=%~2"

if /I "%FUNC%"=="banner"  goto :banner
if /I "%FUNC%"=="printc"  goto :printc
if /I "%FUNC%"=="error"   goto :error
if /I "%FUNC%"=="info"    goto :info
if /I "%FUNC%"=="success" goto :success

echo Unknown helper function: %FUNC%
exit /b 1

:banner
set "L1=  _____   _______      ___      _____    _____     _____ "
set "L2= / ____| |__   __|    /   \    |___  \  |  __ \   / ____|"
set "L3=| |  __     | |      /  ^  \      / /   | |__) | | |     "
set "L4=| | |_ |    | |     /  /_\  \    / /    |  ___/  | |     "
set "L5=| |__| |    | |    /  _____  \  / /___  | |      | |____ "
set "L6= \_____|    |_|   /_/       \_\|______| |_|       \_____|"
set "L7=                  |_|       |_|                          "

set "REF_LEN=57"
for %%L in (1 2 3 4 5 6 7) do (
    set "GRAD_IN=!L%%L!"
    call :_build_gradient
    echo(!GRAD_OUT!
)
exit /b 0

:printc
set "GRAD_IN=!MSG!"
set "REF_LEN=0"
call :_build_gradient
echo(!GRAD_OUT!
exit /b 0

:error
echo(!ESC![1;38;2;255;65;65mERROR:!ESC![0m !MSG!
exit /b 0

:info
set "GRAD_IN=INFO:"
set "REF_LEN=0"
call :_build_gradient
echo(!GRAD_OUT! !MSG!
exit /b 0

:success
echo(!ESC![1;38;2;50;220;100mSUCCESSFUL:!ESC![0m !MSG!
exit /b 0

:_build_gradient
set "GRAD_OUT="
call :_strlen GRAD_IN len
if !len! EQU 0 exit /b 0

set /a "ref=REF_LEN"
if !ref! LEQ 0 set /a "ref=len"
if !ref! GTR 1 (set /a "denom=ref - 1") else (set /a "denom=1")
set /a "last=len - 1"

for /L %%i in (0,1,!last!) do (
    set "ch=!GRAD_IN:~%%i,1!"
    if "!ch!"==" " (
        set "GRAD_OUT=!GRAD_OUT! "
    ) else (
        set /a "pos=%%i * 3000 / denom"
        set /a "seg=pos / 1000"
        set /a "t=pos %% 1000"
        if !seg! GEQ 3 (
            set /a "seg=2"
            set /a "t=1000"
        )
        set /a "next=seg + 1"
        for %%s in (!seg!) do for %%n in (!next!) do (
            set /a "r=GRAD_R_%%s + (GRAD_R_%%n - GRAD_R_%%s) * t / 1000"
            set /a "g=GRAD_G_%%s + (GRAD_G_%%n - GRAD_G_%%s) * t / 1000"
            set /a "b=GRAD_B_%%s + (GRAD_B_%%n - GRAD_B_%%s) * t / 1000"
        )
        set "GRAD_OUT=!GRAD_OUT!!ESC![1;38;2;!r!;!g!;!b!m!ch!"
    )
)
set "GRAD_OUT=!GRAD_OUT!!ESC![0m"
exit /b 0

:_strlen
set "s=!%~1!#"
set "len=0"
for %%P in (4096 2048 1024 512 256 128 64 32 16 8 4 2 1) do (
    if "!s:~%%P,1!" NEQ "" (
        set /a "len+=%%P"
        set "s=!s:~%%P!"
    )
)
set "%~2=!len!"
exit /b 0
